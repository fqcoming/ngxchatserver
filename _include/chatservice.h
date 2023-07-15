#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include <unordered_map>
#include <functional>
#include <mutex>


#include "redis.hpp"
#include "groupmodel.hpp"
#include "friendmodel.hpp"
#include "usermodel.hpp"
#include "offlinemessagemodel.hpp"

#include "ngx_c_socket.h"

#include "json.h"
using json = nlohmann::json;


// 表示处理消息的事件回调方法类型
using MsgHandler = std::function<void(const lpngx_connection_t &conn, json &js, LPSTRUC_MSG_HEADER pMsgHeader)>;

// 聊天服务器业务类
class ChatService
{
public:
    // 获取单例对象的接口函数
    static ChatService *instance();
    // 处理登录业务
    void login(const lpngx_connection_t &conn, json &js, LPSTRUC_MSG_HEADER pMsgHeader);
    // 处理注册业务
    void reg(const lpngx_connection_t &conn, json &js, LPSTRUC_MSG_HEADER pMsgHeader);
    // 一对一聊天业务
    void oneChat(const lpngx_connection_t &conn, json &js, LPSTRUC_MSG_HEADER pMsgHeader);
    // 添加好友业务
    void addFriend(const lpngx_connection_t &conn, json &js, LPSTRUC_MSG_HEADER pMsgHeader);
    // 创建群组业务
    void createGroup(const lpngx_connection_t &conn, json &js, LPSTRUC_MSG_HEADER pMsgHeader);
    // 加入群组业务
    void addGroup(const lpngx_connection_t &conn, json &js, LPSTRUC_MSG_HEADER pMsgHeader);
    // 群组聊天业务
    void groupChat(const lpngx_connection_t &conn, json &js, LPSTRUC_MSG_HEADER pMsgHeader);
    // 处理注销业务
    void loginout(const lpngx_connection_t &conn, json &js, LPSTRUC_MSG_HEADER pMsgHeader);
    // 处理客户端异常退出
    void clientCloseException(const lpngx_connection_t &conn);
    // 服务器异常，业务重置方法
    // void reset();
    // 获取消息对应的处理器
    MsgHandler getHandler(int msgid);
    // 从redis消息队列中获取订阅的消息
    void handleRedisSubscribeMessage(int, string);

private:
    ChatService();

    // 存储消息id和其对应的业务处理方法
    unordered_map<int, MsgHandler> _msgHandlerMap;
    // 存储在线用户的通信连接
    unordered_map<int, lpngx_connection_t> _userConnMap;
    // 定义互斥锁，保证_userConnMap的线程安全
    mutex _connMutex;

    // 数据操作类对象
    UserModel _userModel;
    OfflineMsgModel _offlineMsgModel;
    FriendModel _friendModel;
    GroupModel _groupModel;

    // redis操作对象
    Redis _redis;
};

#endif