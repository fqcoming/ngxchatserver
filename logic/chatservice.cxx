
#include "public.h"
#include "ngx_func.h"
#include "json.h"
#include "usermodel.hpp"
#include "user.hpp"
#include "chatservice.h"
#include "ngx_c_memory.h"
#include "ngx_global.h"
#include "ngx_c_crc32.h"
#include "ngx_logiccomm.h"
#include <arpa/inet.h>

#include <iostream>
#include <vector>

using namespace placeholders;


// 获取单例对象的接口函数
ChatService *ChatService::instance()
{
    static ChatService service;
    return &service;
}

// 注册消息以及对应的Handler回调操作
ChatService::ChatService()
{
    // 用户基本业务管理相关事件处理回调注册
    _msgHandlerMap.insert({LOGIN_MSG, bind(&ChatService::login, this, _1, _2, _3)});
    _msgHandlerMap.insert({LOGINOUT_MSG, std::bind(&ChatService::loginout, this, _1, _2, _3)});
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, _1, _2, _3)});

    // 群组业务管理相关事件处理回调注册
    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});

    // 连接redis服务器
    if (_redis.connect())
    {
        // 设置上报消息的回调
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    }
}


// 获取消息对应的处理器
MsgHandler ChatService::getHandler(int msgid)
{
    // 记录错误日志，msgid没有对应的事件处理回调
    auto it = _msgHandlerMap.find(msgid);
    if (it == _msgHandlerMap.end())
    {
        // 返回一个默认的处理器，空操作
        return [=](const lpngx_connection_t &conn, json &js, LPSTRUC_MSG_HEADER pMsgHeader) {
            // LOG_ERROR << "msgid:" << msgid << " can not find handler!";
            ngx_log_stderr(0,"msgid: %d can not find handler!", msgid);
        };
    }
    else
    {
        return _msgHandlerMap[msgid];
    }
}



#if 0

// 服务器异常，业务重置方法
void ChatService::reset()
{
    // 把online状态的用户，设置成offline
    _userModel.resetState();
}

#endif



static void responseSend(const lpngx_connection_t &conn, json &response, LPSTRUC_MSG_HEADER pMsgHeader) {

    string res = response.dump();

    // 消息头 + 包头 + 包体
    
    CMemory  *p_memory = CMemory::GetInstance();
    char *p_sendbuf = (char *)p_memory->AllocMemory(1024, true);
    memcpy(p_sendbuf, pMsgHeader, sizeof(STRUC_MSG_HEADER));  
    printf("### 消息头：%ld ###\n", (*(STRUC_MSG_HEADER*)(p_sendbuf)).iCurrsequence);

    //c)填充包头
    LPCOMM_PKG_HEADER pPkgHeader;
    pPkgHeader = (LPCOMM_PKG_HEADER)(p_sendbuf+sizeof(STRUC_MSG_HEADER));    //指向包头
    pPkgHeader->msgCode = _CMD_ONMESSAGE;	                        //消息代码，可以统一在ngx_logiccomm.h中定义
    pPkgHeader->msgCode = htons(pPkgHeader->msgCode);	            //htons主机序转网络序 
    pPkgHeader->pkgLen  = htons(sizeof(COMM_PKG_HEADER) + res.size()); 

    printf("@@@ msgid: %d, msglen: %d\n", ntohs(pPkgHeader->msgCode), ntohs(pPkgHeader->pkgLen));
    printf("@@@ msgid: %d, msglen: %d\n", pPkgHeader->msgCode, pPkgHeader->pkgLen);


    //d)填充包体
    char *pkgbdy = (p_sendbuf+sizeof(STRUC_MSG_HEADER)+sizeof(COMM_PKG_HEADER));	//跳过消息头，跳过包头，就是包体了
    //。。。。。这里根据需要，填充要发回给客户端的内容,int类型要使用htonl()转，short类型要使用htons()转；
    memcpy(pkgbdy, res.c_str(), res.size());
    
    //e)包体内容全部确定好后，计算包体的crc32值
    CCRC32   *p_crc32 = CCRC32::GetInstance();
    pPkgHeader->crc32 = p_crc32->Get_CRC((unsigned char *)pkgbdy, res.size());
    pPkgHeader->crc32 = htonl(pPkgHeader->crc32);	

    std::cout << "加入发送队列" << endl;

    g_socket.msgSend(p_sendbuf);
}


// 处理登录业务  id  pwd   pwd
void ChatService::login(const lpngx_connection_t &conn, json &js, LPSTRUC_MSG_HEADER pMsgHeader)
{
    cout << "enter login" << endl;
    cout << js.dump() << endl;

    int id = js["id"].get<int>();
    string pwd = js["password"];

    User user = _userModel.query(id);

    if (user.getId() == id && user.getPwd() == pwd)
    {
        if (user.getState() == "online")
        {
            // 该用户已经登录，不允许重复登录
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "this account is using, input another!";

            cout << response.dump() << endl;
            // conn->send(response.dump());
            responseSend(conn, response, pMsgHeader);
        }
        else
        {
            cout << user.getName() << "登录成功，记录用户连接信息" << endl;

            // 登录成功，记录用户连接信息
            {
                lock_guard<mutex> lock(_connMutex);
                _userConnMap.insert({id, conn});
            }

            // std::cout << "开始订阅" << std::endl;
            // id用户登录成功后，向redis订阅channel(id)
            _redis.subscribe(id); 
            // std::cout << "订阅成功" << std::endl;

            // 登录成功，更新用户状态信息 state offline=>online
            user.setState("online");
            bool ret = _userModel.updateState(user);
            if (!ret) cout << "失败" << endl;

            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 0;
            response["id"] = user.getId();
            response["name"] = user.getName();

            // 查询该用户是否有离线消息
            vector<string> vec = _offlineMsgModel.query(id);
            if (!vec.empty())
            {
                response["offlinemsg"] = vec;
                // 读取该用户的离线消息后，把该用户的所有离线消息删除掉
                _offlineMsgModel.remove(id);
            }

            // 查询该用户的好友信息并返回
            vector<User> userVec = _friendModel.query(id);
            if (!userVec.empty())
            {
                vector<string> vec2;
                for (User &user : userVec)
                {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec2.push_back(js.dump());
                }
                response["friends"] = vec2;
            }

            // 查询用户的群组信息
            vector<Group> groupuserVec = _groupModel.queryGroups(id);
            if (!groupuserVec.empty())
            {
                // group:[{groupid:[xxx, xxx, xxx, xxx]}]
                vector<string> groupV;
                for (Group &group : groupuserVec)
                {
                    json grpjson;
                    grpjson["id"] = group.getId();
                    grpjson["groupname"] = group.getName();
                    grpjson["groupdesc"] = group.getDesc();
                    vector<string> userV;
                    for (GroupUser &user : group.getUsers())
                    {
                        json js;
                        js["id"] = user.getId();
                        js["name"] = user.getName();
                        js["state"] = user.getState();
                        js["role"] = user.getRole();
                        userV.push_back(js.dump());
                    }
                    grpjson["users"] = userV;
                    groupV.push_back(grpjson.dump());
                }

                response["groups"] = groupV;
            }

#if 0
            string res = response.dump();
            // 消息头 + 包头 + 包体

            CMemory  *p_memory = CMemory::GetInstance();
            char *p_sendbuf = (char *)p_memory->AllocMemory(1024, true);
            memcpy(p_sendbuf, pMsgHeader, sizeof(STRUC_MSG_HEADER));  
            printf("### 消息头：%ld ###\n", (*(STRUC_MSG_HEADER*)(p_sendbuf)).iCurrsequence);

            //c)填充包头
            LPCOMM_PKG_HEADER pPkgHeader;
            pPkgHeader = (LPCOMM_PKG_HEADER)(p_sendbuf+sizeof(STRUC_MSG_HEADER));    //指向包头
            pPkgHeader->msgCode = _CMD_ONMESSAGE;	                        //消息代码，可以统一在ngx_logiccomm.h中定义
            pPkgHeader->msgCode = htons(pPkgHeader->msgCode);	            //htons主机序转网络序 
            pPkgHeader->pkgLen  = htons(sizeof(COMM_PKG_HEADER) + res.size()); 

            printf("@@@ msgid: %d, msglen: %d\n", ntohs(pPkgHeader->msgCode), ntohs(pPkgHeader->pkgLen));
            printf("@@@ msgid: %d, msglen: %d\n", pPkgHeader->msgCode, pPkgHeader->pkgLen);


            //d)填充包体
            char *pkgbdy = (p_sendbuf+sizeof(STRUC_MSG_HEADER)+sizeof(COMM_PKG_HEADER));	//跳过消息头，跳过包头，就是包体了
            //。。。。。这里根据需要，填充要发回给客户端的内容,int类型要使用htonl()转，short类型要使用htons()转；
            memcpy(pkgbdy, res.c_str(), res.size());
            
            //e)包体内容全部确定好后，计算包体的crc32值
            CCRC32   *p_crc32 = CCRC32::GetInstance();
            pPkgHeader->crc32 = p_crc32->Get_CRC((unsigned char *)pkgbdy, res.size());
            pPkgHeader->crc32 = htonl(pPkgHeader->crc32);	

            std::cout << "加入发送队列" << endl;

            g_socket.msgSend(p_sendbuf);

#else 
            responseSend(conn, response, pMsgHeader);

#endif
        }

    }
    else
    {
        // 该用户不存在，用户存在但是密码错误，登录失败
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "id or password is invalid!";
        // conn->send(response.dump());
        responseSend(conn, response, pMsgHeader);
    }
}




// 处理注册业务  name  password
void ChatService::reg(const lpngx_connection_t &conn, json &js, LPSTRUC_MSG_HEADER pMsgHeader)
{
    string regmsg = js.dump();
    printf("注册消息: %s\n", regmsg.c_str());
    string name = js["name"];
    string pwd = js["password"];

    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state = _userModel.insert(user);
    if (state)
    {
        // 注册成功
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 0;
        response["id"] = user.getId();

        responseSend(conn, response, pMsgHeader);
        // conn->send(response.dump());
    }
    else
    {
        // 注册失败
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1;
        // conn->send(response.dump());
        responseSend(conn, response, pMsgHeader);
    }
}



// 处理注销业务
void ChatService::loginout(const lpngx_connection_t &conn, json &js, LPSTRUC_MSG_HEADER pMsgHeader)
{
    int userid = js["id"].get<int>();

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if (it != _userConnMap.end())
        {
            _userConnMap.erase(it);
        }
    }

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(userid); 

    // 更新用户的状态信息
    User user(userid, "", "", "offline");
    _userModel.updateState(user);
}


// 处理客户端异常退出
void ChatService::clientCloseException(const lpngx_connection_t &conn)
{
    User user;
    {
        lock_guard<mutex> lock(_connMutex);
        for (auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it)
        {
            if (it->second == conn)
            {
                // 从map表删除用户的链接信息
                user.setId(it->first);
                _userConnMap.erase(it);
                break;
            }
        }
    }

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(user.getId()); 

    // 更新用户的状态信息
    if (user.getId() != -1)
    {
        user.setState("offline");
        _userModel.updateState(user);
    }
}





// 一对一聊天业务
void ChatService::oneChat(const lpngx_connection_t &conn, json &js, LPSTRUC_MSG_HEADER pMsgHeader)
{
    int toid = js["toid"].get<int>();

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if (it != _userConnMap.end())
        {
            // toid在线，转发消息   服务器主动推送消息给toid用户
            // it->second->send(js.dump());
            pMsgHeader->pConn = it->second;
            pMsgHeader->iCurrsequence = it->second->iCurrsequence;
            printf("^^^^^^^^^^^^^^^^^^^^^\n");
            responseSend(it->second, js, pMsgHeader);
            return;
        }
    }

    printf("nononononnnnnnnnnnnnnnnnn\n");
    // 查询toid是否在线 
    User user = _userModel.query(toid);
    if (user.getState() == "online")
    {
        _redis.publish(toid, js.dump());
        return;
    }

    printf("[[[[[[[[[[[[[[[]]]]]]]]]]]]]]]\n");
    // toid不在线，存储离线消息
    _offlineMsgModel.insert(toid, js.dump());
}





// 添加好友业务 msgid id friendid
void ChatService::addFriend(const lpngx_connection_t &conn, json &js, LPSTRUC_MSG_HEADER pMsgHeader)
{
    int userid = js["id"].get<int>();
    int friendid = js["friendid"].get<int>();

    // 存储好友信息
    _friendModel.insert(userid, friendid);
}





// 创建群组业务
void ChatService::createGroup(const lpngx_connection_t &conn, json &js, LPSTRUC_MSG_HEADER pMsgHeader)
{
    int userid = js["id"].get<int>();
    string name = js["groupname"];
    string desc = js["groupdesc"];

    // 存储新创建的群组信息
    Group group(-1, name, desc);
    if (_groupModel.createGroup(group))
    {
        // 存储群组创建人信息
        _groupModel.addGroup(userid, group.getId(), "creator");
    }
}



// 加入群组业务
void ChatService::addGroup(const lpngx_connection_t &conn, json &js, LPSTRUC_MSG_HEADER pMsgHeader)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    _groupModel.addGroup(userid, groupid, "normal");
}




// 群组聊天业务
void ChatService::groupChat(const lpngx_connection_t &conn, json &js, LPSTRUC_MSG_HEADER pMsgHeader)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    vector<int> useridVec = _groupModel.queryGroupUsers(userid, groupid);

    lock_guard<mutex> lock(_connMutex);
    for (int id : useridVec)
    {
        auto it = _userConnMap.find(id);
        if (it != _userConnMap.end())
        {
            // 转发群消息
            pMsgHeader->pConn = it->second;
            responseSend(it->second, js, pMsgHeader);
        }
        else
        {
            // 查询toid是否在线 
            User user = _userModel.query(id);
            if (user.getState() == "online")
            {
                _redis.publish(id, js.dump());
            }
            else
            {
                // 存储离线群消息
                _offlineMsgModel.insert(id, js.dump());
            }
        }
    }
}


// 从redis消息队列中获取订阅的消息
void ChatService::handleRedisSubscribeMessage(int userid, string msg)
{
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end())
    {
        // it->second->send(msg);
        return;
    }

    // 存储该用户的离线消息
    // _offlineMsgModel.insert(userid, msg);
}
