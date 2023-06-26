#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <atomic>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <semaphore.h>

#include "json.hpp"
#include "group.hpp"
#include "user.hpp"
#include "public.hpp"
using namespace std;
using json = nlohmann::json;

// 记录当前系统登录的用户信息
User g_currentUser;
// 记录当前登录用户的好友列表信息
vector<User> g_currentUserFriendList;
// 记录当前登录用户的群组列表信息
vector<Group> g_currentUserGroupList;

// 控制主菜单页面程序，记录登录状态
bool isMainMenuRunning = false;
// 用于读写线程之间的通信
sem_t rwsem;
// 记录登录状态
atomic_bool g_isLoginSuccess{false};

// 接受消息  子线程
void readTaskHandler(int clientfd);
// 获取系统时间（聊天信息需要添加时间）
string getcurrentTime();
// 主聊天页面程序
void mainMenu(int);
// 显示当前登录用户的基本信息
void showCurrentUserData();

// 聊天客户端程序实现，main程序用于发送消息，子线程用于接收消息
int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        cerr << "command invalid! please put following example: ./ChatClient 127.0.0.1 6000" << endl;
        exit(-1);
    }
    // 解析出IP+PORT
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    // 创建client端的socket
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd == -1)
    {
        cerr << "socket create error" << endl;
        exit(-1);
    }
    // 绑定socket的地址结构
    sockaddr_in server;
    memset(&server, 0, sizeof(sockaddr_in));
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    // client和server进行连接
    if (-1 == connect(clientfd, (sockaddr *)&server, sizeof(sockaddr_in)))
    {
        cerr << "connect server error" << endl;
        close(clientfd);
        exit(-1);
    }

    // 初始化读写线程通信的信号量
    sem_init(&rwsem, 0, 0);

    // 连接服务器成功，启动接受消息的子线程。子线程接收消息的同时，会处理登录和注册的响应消息
    std::thread readTask(readTaskHandler, clientfd);
    readTask.detach();

    // main线程用于接收用户输入，负责发送数据
    for (;;)
    {
        // 显示首页面菜单 登录、注册、退出
        cout << "========================" << endl;
        cout << "1. login" << endl;
        cout << "2. register" << endl;
        cout << "3. quit" << endl;
        cout << "========================" << endl;
        cout << "choice:";
        int choice = 0;
        cin >> choice;
        cin.get(); // 读掉缓冲区残留的回车  c/c++中输入整数会包含一个回车，影响后续字符串读入
        switch (choice)
        {
        case 1: // login业务
        {
            int id = 0;
            char pwd[50] = {0};
            cout << "userid:";
            cin >> id;
            cin.get(); // 读掉缓冲区残留的回车
            cout << "userpassword:";
            cin.getline(pwd, 50); // cin>>和sanf()，输入时包含空格和非法字符，会返回导致输入失败

            json js;
            js["msgid"] = LOGIN_MSG;
            js["id"] = id;
            js["password"] = pwd;
            string request = js.dump();

            g_isLoginSuccess = false;

            int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (-1 == len)
            {
                cerr << "send login msg error" << request << endl;
            }
            sem_wait(&rwsem); // 等待子线程处理登录的响应消息后，唤醒此处的信号量
            if (g_isLoginSuccess)
            {
                // 进入聊天主菜单页面
                isMainMenuRunning = true;
                mainMenu(clientfd);
            }
        }
        break;

        case 2: // register业务
        {
            char name[50] = {0};
            char pwd[50] = {0};
            cout << "username:";
            cin.getline(name, 50);
            cout << "userpassword:";
            cin.getline(pwd, 50);

            json js; // 组装json数据
            js["msgid"] = REG_MSG;
            js["name"] = name;
            js["password"] = pwd;
            string request = js.dump(); // 序列化

            int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0); // 发送json数据给server
            if (len == -1)
            {
                cerr << "send reg msg error:" << request << endl;
            }
            sem_wait(&rwsem);    //子线程处理完注册响应会唤醒此处，回到登录页
        }
        break;

        case 3: // quit业务
            close(clientfd);
            sem_destroy(&rwsem);
            exit(0);
        default:
            cerr << "invalid input !" << endl;
            break;
        }
    }

    return 0;
}

// 子线程处理登录后的业务逻辑
void doLoginResponse(json &responsejs)
{
    if (0 != responsejs["errno"].get<int>()) // errno = 1 登录失败
    {
        cerr << responsejs["errmsg"] << endl;
        g_isLoginSuccess = false;
    }
    else // 登录成功
    {
        // 记录当前登录用户的id和name
        g_currentUser.setId(responsejs["id"].get<int>());
        g_currentUser.setName(responsejs["name"]);

        // 记录当前用户的好友列表消息
        if (responsejs.contains("friends")) // 判断返回的json数据是否包含friends字段
        {
            // 初始化，防止登出后，重新登录时，好友列表信息和群组列表信息push_back两次
            g_currentUserFriendList.clear();
            vector<string> vec = responsejs["friends"];
            for (string &str : vec)
            {
                json js = json::parse(str);
                User user;
                user.setId(js["id"].get<int>());
                user.setName(js["name"]);
                user.setState(js["state"]);
                g_currentUserFriendList.push_back(user);
            }
        }

        // 记录当前用户的群组列表信息
        if (responsejs.contains("groups")) // 判断返回的json数据是否包含groups字段
        {
            g_currentUserGroupList.clear();

            vector<string> vec1 = responsejs["groups"];
            for (string &grpstr : vec1)
            {
                json grpjs = json::parse(grpstr);
                Group group;
                group.setId(grpjs["id"].get<int>());
                group.setName(grpjs["groupname"]);
                group.setDesc(grpjs["groupdesc"]);

                vector<string> vec2 = grpjs["users"];
                for (string &userstr : vec2)
                {
                    json js = json::parse(userstr);
                    GroupUser user;
                    user.setId(js["id"].get<int>());
                    user.setName(js["name"]);
                    user.setState(js["state"]);
                    user.setRole(js["role"]);
                    group.getUsers().push_back(user);
                }

                g_currentUserGroupList.push_back(group);
            }
        }

        // 显示用户当前信息，包括用户信息，好友信息，群组信息
        showCurrentUserData();

        // 显示当前用户离线消息，个人聊天信息和群组聊天信息
        if (responsejs.contains("offlinemessage"))
        {
            vector<string> vec = responsejs["offlinemessage"];
            for (string &str : vec)
            {
                json js = json::parse(str);
                // time + [id] + name + "said:"+xx
                if (ONE_CHAT_MSG == js["msgid"].get<int>())
                {
                    cout << js["time"].get<string>() << " [" << js["id"] << "] " << js["name"].get<string>() << " said: " << js["msg"].get<string>() << endl;
                }
                else
                {
                    cout << "group message[" << js["groupid"] << "] " << js["time"].get<string>() << " [" << js["id"] << "] " << js["name"].get<string>() << " said: " << js["msg"].get<string>() << endl;
                }
            }
        }
        g_isLoginSuccess = true;
    }
}
// 子线程处理注册响应逻辑
void doRegResponse(json &responsejs)
{
    if (0 != responsejs["errno"].get<int>()) // server返回的reg消息中，errno=0 注册成功，errno=1 失败
    {
        cerr << "name is already exist, register failed!" << endl;
    }
    else // reg消息errno=0，注册成功
    {
        cout << "account registers successfully, userid is " << responsejs["id"] << ", remember it!" << endl;
    }
}
// 接受消息  子线程
void readTaskHandler(int clientfd)
{
    for (;;)
    {
        char buffer[1024] = {0};
        int len = recv(clientfd, buffer, 1024, 0);
        if (-1 == len || 0 == len)
        {
            close(clientfd);
            exit(-1);
        }
        // 接收ChatServer发送的消息
        json js = json::parse(buffer);
        int msgtype = js["msgid"].get<int>();
        if (ONE_CHAT_MSG == msgtype)
        {
            cout << js["time"].get<string>() << " [" << js["id"] << "] " << js["name"].get<string>() << " said: " << js["msg"].get<string>() << endl;
            continue;
        }
        if (GROUP_CHAT_MSG == msgtype)
        {
            cout << "group message[" << js["groupid"] << "] " << js["time"].get<string>() << " [" << js["id"] << "] " << js["name"].get<string>() << " said: " << js["msg"].get<string>() << endl;
            continue;
        }
        if (LOGIN_MSG_ACK == msgtype)
        {
            doLoginResponse(js); // 处理登录后的业务逻辑
            sem_post(&rwsem);    // 通知主线程登录的结果
            continue;
        }
        if (REG_MSG_ACK == msgtype)
        {
            doRegResponse(js);
            sem_post(&rwsem); // 通知主线程注册的结果
            continue;
        }
    }
}

// 显示当前登录用户的基本信息
void showCurrentUserData()
{
    cout << "======================login user======================" << endl;
    cout << "current login user => id:" << g_currentUser.getId() << " name:" << g_currentUser.getName() << endl;
    cout << "----------------------friend list---------------------" << endl;
    if (!g_currentUserFriendList.empty())
    {
        for (User &user : g_currentUserFriendList)
        {
            cout << user.getId() << " " << user.getName() << " " << user.getState() << endl;
        }
    }
    cout << "----------------------group list----------------------" << endl;
    if (!g_currentUserGroupList.empty())
    {
        for (Group &group : g_currentUserGroupList)
        {
            cout << group.getId() << " " << group.getName() << " " << group.getDesc() << endl;
            for (GroupUser &user : group.getUsers())
            {
                cout << user.getId() << " " << user.getName() << " " << user.getState()
                     << " " << user.getRole() << endl;
            }
        }
    }
    cout << "======================================================" << endl;
}

// 获取系统时间（聊天信息需要添加时间）
string getcurrentTime()
{
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm *ptm = localtime(&tt);
    char date[60] = {0};
    sprintf(date, "%d-%02d-%02d %02d:%02d:%02d",
            (int)ptm->tm_year + 1900, (int)ptm->tm_mon + 1, (int)ptm->tm_mday,
            (int)ptm->tm_hour, (int)ptm->tm_min, (int)ptm->tm_sec);
    return string(date);
}

void help(int fd = 0, string str = "");
void chat(int, string);
void addfriend(int, string);
void creategroup(int, string);
void addgroup(int, string);
void groupchat(int, string);
void loginout(int, string);

// 系统支持的客户端命令列表
unordered_map<string, string> commandMap = {
    {"help", "显示所有支持的命令, 格式help"},
    {"chat", "一对一聊天, 格式chat:friendid:message"},
    {"addfriend", "添加好友, 格式createfriend:friendid"},
    {"creategroup", "创建群组, 格式creatgroup:groupname:groupdesc"},
    {"addgroup", "加入群组, 格式addgroup:groupid"},
    {"groupchat", "群聊, 格式groupchat:groupid:message"},
    {"loginout", "注销, 格式loginout"}};
// 注册系统支持的客户端命令处理
unordered_map<string, function<void(int, string)>> commandHandlerMap = {
    {"help", help},
    {"chat", chat},
    {"addfriend", addfriend},
    {"creategroup", creategroup},
    {"addgroup", addgroup},
    {"groupchat", groupchat},
    {"loginout", loginout}};

// 主聊天页面程序
void mainMenu(int clientfd)
{
    help();

    char buffer[1024] = {0};
    while (isMainMenuRunning)
    {
        cin.getline(buffer, 1024);
        string commandbuf(buffer);      // buffer ---> chat:friendid:message 此类型
        string command;                 // 存储命令
        int idx = commandbuf.find(":"); // 拆分输入的命令
        if (-1 == idx)
        {
            command = commandbuf; // 输入为help / loginout
        }
        else
        {
            command = commandbuf.substr(0, idx); // 输入为chat:xxx
        }

        auto it = commandHandlerMap.find(command); // 从commandHandlerMap查找对应命令
        if (it == commandHandlerMap.end())
        {
            cerr << "invalid input command!" << endl;
            continue;
        }

        // 找到命令，调用相应命令的事件处理回调，mainMenu对修改封闭，添加功能不需要修改该函数
        it->second(clientfd, commandbuf.substr(idx + 1, commandbuf.size() - idx)); // : 前为命令对应的回调函数，：后为命令参数
    }
}

void help(int, string) //"help" command handler
{
    cout << "show command list >>>" << endl;
    for (auto &p : commandMap)
    {
        cout << p.first << " : " << p.second << endl;
    }
    cout << endl;
}

void chat(int clientfd, string str) //"chat" command handler   str=friendid:message
{
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "chat command invalid!" << endl;
        return;
    }
    int friendid = atoi(str.substr(0, idx).c_str());
    string message = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = ONE_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["toid"] = friendid;
    js["msg"] = message;
    js["time"] = getcurrentTime();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send chat msg error -> " << buffer << endl;
    }
}

void addfriend(int clientfd, string str) //"addfriend" command handler
{
    int friendid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_FRIEND_MSG;
    js["id"] = g_currentUser.getId();
    js["friendid"] = friendid;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send addfriend msg error -> " << buffer << endl;
    }
}

void creategroup(int clientfd, string str) //"creategroup" command handler
{
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "creategroup command invalid!" << endl;
        return;
    }
    string groupname = str.substr(0, idx);
    string groupdesc = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = CREATE_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupname"] = groupname;
    js["groupdesc"] = groupdesc;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send creategroup msg error" << endl;
    }
}

void addgroup(int clientfd, string str) //"addgroup" command handler
{
    int groupid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupid"] = groupid;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send addgroup msg error -> " << buffer << endl;
    }
}

void groupchat(int clientfd, string str) //"groupchat" command handler   groupid:message
{
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "groupchat command invalid!" << endl;
        return;
    }
    int groupid = atoi(str.substr(0, idx).c_str());
    string message = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = GROUP_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["groupid"] = groupid;
    js["msg"] = message;
    js["time"] = getcurrentTime();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send groupchat msg error" << endl;
    }
}
void loginout(int clientfd, string str) //"loginout" command handler
{
    json js;
    js["msgid"] = LOGINOUT_MSG;
    js["id"] = g_currentUser.getId();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send loginout msg error -> " << buffer << endl;
    }
    else
    {
        isMainMenuRunning = false;
    }
}
