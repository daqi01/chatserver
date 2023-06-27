# chatserver
基于muduo实现的可以工作于niginx tcp负载均衡环境下的集群聊天服务器和客户端，主要技术包括Redis、MySQL、Nginx、网络编程，多线程

0. 基于Linux环境，必要安装muduo，mysql，nginx，redis
1. 进入build目录，执行rm -rf *
2. 执行cmake .. ，编译项目
3. 执行make
4. 运行服务器./ChatServer 127.0.0.1 6000，后者为服务器ip+port
5. 运行客户端./ChatClient 127.0.0.1 8000，后者为nginx服务器ip+port
