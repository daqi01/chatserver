#ifndef FRIENDMODEL_H
#define FRIENDMODEL_H

#include "user.hpp"
#include <vector>
using namespace std;

// 维护好友信息的接口方法
class FriendModel
{
public:
    // 添加好友关系
    void insert(int userid, int friendid);
    // 返回用户好友列表 包括好友id、姓名、状态
    vector<User> query(int userid);
};

#endif