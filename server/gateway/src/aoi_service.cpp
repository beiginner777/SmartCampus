#include "aoi_service.h"

#include "tcp_server.h"

#include "logger.h"

#include <json/json.h>

#include <string>

namespace gateway {

namespace {

std::string json_to_string(const Json::Value& v) {
    Json::FastWriter w;
    std::string s = w.write(v);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

void send_json(const AoiService::SessionPtr& s, uint16_t msg_type, uint32_t seq,
               const Json::Value& v) {
    s->send_packet(msg_type, seq, json_to_string(v));
}

// 组装 M2 广播消息 body
std::string make_user_enter(uint32_t uid, double lat, double lng, const std::string& nickname) {
    Json::Value v;
    v["uid"] = uid;
    v["lng"] = lng;
    v["lat"] = lat;
    v["nickname"] = nickname;
    return json_to_string(v);
}

std::string make_user_leave(uint32_t uid) {
    Json::Value v;
    v["uid"] = uid;
    return json_to_string(v);
}

std::string make_position_update(uint32_t uid, double lat, double lng) {
    Json::Value v;
    v["uid"] = uid;
    v["lng"] = lng;
    v["lat"] = lat;
    return json_to_string(v);
}

}  // namespace

AoiService::AoiService(TcpServer& tcp, common::RedisPool& redis)
    : tcp_(tcp), redis_(redis) {
    using common::MsgType;
    router_.register_handler(static_cast<uint16_t>(MsgType::kPositionReportReq),
                             [this](SessionPtr s, uint32_t seq, const std::string& b) {
                                 handle_position_report(s, seq, b);
                             });
}

void AoiService::handle_packet(SessionPtr session, const common::Packet& pkt) {
    if (!router_.dispatch(pkt.msg_type, session, pkt.seq, pkt.body)) {
        LOG_WARN("未注册消息类型 0x{:04x} [{}]", pkt.msg_type, session->remote());
        send_error(session, pkt.seq, 6, "未知消息类型");
    }
}

void AoiService::handle_position_report(const SessionPtr& session, uint32_t seq,
                                        const std::string& body) {
    if (!session->authenticated()) {
        send_error(session, seq, 4, "未登录");
        return;
    }
    Json::Value req;
    Json::Reader reader;
    if (!reader.parse(body, req)) {
        send_error(session, seq, 1, "参数解析失败");
        return;
    }
    double lng = req.isMember("lng") ? req["lng"].asDouble() : 0.0;
    double lat = req.isMember("lat") ? req["lat"].asDouble() : 0.0;
    uint32_t uid = session->uid();

    // 写 Redis 坐标缓存（供跨网关 / 找搭子 / 帖子过滤查询）
    redis_.set("coord:" + std::to_string(uid), std::to_string(lat) + "," + std::to_string(lng));

    if (!aoi_.contains(uid)) {
        // 首次上报：纳入网格，回 nearby + 广播 USER_ENTER
        auto nearby = aoi_.enter(uid, lat, lng);
        Json::Value resp;
        resp["code"] = 0;
        Json::Value arr(Json::arrayValue);
        for (auto& n : nearby) {
            Json::Value o;
            o["uid"] = n.uid;
            o["lng"] = n.lng;
            o["lat"] = n.lat;
            o["nickname"] = nickname_of(n.uid);
            arr.append(o);
        }
        resp["nearby"] = arr;
        send_json(session, static_cast<uint16_t>(common::MsgType::kPositionReportResp), seq, resp);

        std::string enter_body = make_user_enter(uid, lat, lng, session->nickname());
        for (auto& n : nearby) {
            tcp_.send_to(n.uid, static_cast<uint16_t>(common::MsgType::kUserEnter), enter_body);
        }
        return;
    }

    // 移动：广播进入 / 离开 / 更新
    auto diff = aoi_.move(uid, lat, lng);
    std::string enter_body = make_user_enter(uid, lat, lng, session->nickname());
    std::string update_body = make_position_update(uid, lat, lng);
    std::string leave_body = make_user_leave(uid);

    for (auto& n : diff.entered) {
        tcp_.send_to(n.uid, static_cast<uint16_t>(common::MsgType::kUserEnter), enter_body);
        session->send_packet(static_cast<uint16_t>(common::MsgType::kUserEnter), 0,
                             make_user_enter(n.uid, n.lat, n.lng, nickname_of(n.uid)));
    }
    for (auto& n : diff.left) {
        tcp_.send_to(n.uid, static_cast<uint16_t>(common::MsgType::kUserLeave), leave_body);
        session->send_packet(static_cast<uint16_t>(common::MsgType::kUserLeave), 0,
                             make_user_leave(n.uid));
    }
    for (auto& n : diff.stayed) {
        tcp_.send_to(n.uid, static_cast<uint16_t>(common::MsgType::kPositionUpdate), update_body);
    }

    Json::Value resp;
    resp["code"] = 0;
    send_json(session, static_cast<uint16_t>(common::MsgType::kPositionReportResp), seq, resp);
}

void AoiService::handle_disconnect(const SessionPtr& session, const std::string& /*reason*/) {
    if (!session->authenticated()) {
        return;
    }
    remove_from_aoi(session->uid());
}

void AoiService::remove_from_aoi(uint32_t uid) {
    auto neighbors = aoi_.leave(uid);
    redis_.del("coord:" + std::to_string(uid));
    if (neighbors.empty()) {
        return;
    }
    std::string body = make_user_leave(uid);
    for (auto& n : neighbors) {
        tcp_.send_to(n.uid, static_cast<uint16_t>(common::MsgType::kUserLeave), body);
    }
}

std::string AoiService::nickname_of(uint32_t uid) {
    auto s = tcp_.find_session(uid);
    return s ? s->nickname() : "";
}

void AoiService::send_error(const SessionPtr& session, uint32_t seq, int code,
                            const std::string& msg) {
    Json::Value v;
    v["code"] = code;
    v["msg"] = msg;
    send_json(session, static_cast<uint16_t>(common::MsgType::kError), seq, v);
}

}  // namespace gateway
