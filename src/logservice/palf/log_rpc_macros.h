/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OCEANBASE_LOGSERVICE_LOG_RPC_MACROS_
#define OCEANBASE_LOGSERVICE_LOG_RPC_MACROS_

#define DEFINE_RPC_PROCESSOR(CLASS, PROXY, REQTYPE, PCODE)                                                        \
  class CLASS : public obrpc::ObRpcProcessor<PROXY::ObRpc<PCODE>>                                                 \
  {                                                                                                               \
  public:                                                                                                         \
    CLASS() : palf_env_impl_(NULL), filter_(NULL) {}                                                              \
    virtual ~CLASS() {}                                                                                           \
    int process()                                                                                                 \
    {                                                                                                             \
      int ret = OB_SUCCESS;                                                                                       \
      LogRpcPacketImpl<REQTYPE> &rpc_packet = arg_;                                                               \
      const REQTYPE &req = rpc_packet.req_;                                                                       \
      const ObAddr server = rpc_packet.src_;                                                                      \
      int64_t palf_id = rpc_packet.palf_id_;                                                                      \
      if (OB_ISNULL(palf_env_impl_) && OB_FAIL(__get_palf_env_impl(rpc_pkt_->get_tenant_id(), palf_env_impl_))) { \
        PALF_LOG(WARN, "__get_palf_env_impl failed", K(ret), KPC(rpc_pkt_));                                      \
      } else if (NULL != filter_ && true == (*filter_)(server)) {                                                 \
        PALF_LOG(INFO, "need filter this packet", K(rpc_packet));                                                 \
      } else {                                                                                                    \
        LogRequestHandler handler(palf_env_impl_);                                                                \
        ret = handler.handle_request(palf_id, server, req);                                                       \
        PALF_LOG(TRACE, "Processor handle_request success", K(ret), K(palf_id), K(req), KP(filter_));             \
      }                                                                                                           \
      return ret;                                                                                                 \
    }                                                                                                             \
    void set_palf_env_impl(void *palf_env_impl, void *filter)                                                     \
    {                                                                                                             \
      filter_ = reinterpret_cast<ObFunction<bool(const ObAddr &src)> *>(filter);                                  \
      palf_env_impl_ = reinterpret_cast<PalfEnvImpl *>(palf_env_impl);                                            \
    }                                                                                                             \
                                                                                                                  \
  private:                                                                                                        \
    PalfEnvImpl *palf_env_impl_;                                                                                  \
    ObFunction<bool(const ObAddr &src)> *filter_;                                                                 \
  }

#define DECLARE_RPC_PROXY_POST_FUNCTION(PRIO, REQTYPE, PCODE)               \
  RPC_AP(PRIO post_packet, PCODE, (palf::LogRpcPacketImpl<palf::REQTYPE>)); \
  int post_packet(const common::ObAddr &dst, const palf::LogRpcPacketImpl<palf::REQTYPE> &pkt, const int64_t tenant_id)

#define DEFINE_RPC_PROXY_POST_FUNCTION(REQTYPE, PCODE)                                              \
  int LogRpcProxyV2::post_packet(const common::ObAddr &dst, const palf::LogRpcPacketImpl<palf::REQTYPE> &pkt, \
                                 const int64_t tenant_id)                                                     \
  {                                                                                                           \
    int ret = common::OB_SUCCESS;                                                                             \
    static obrpc::LogRpcCB<obrpc::PCODE> cb;                                                                  \
    ret = this->to(dst)                                                                                       \
              .timeout(3000 * 1000)                                                                           \
              .trace_time(true)                                                                               \
              .max_process_handler_time(100 * 1000)                                                           \
              .by(tenant_id)                                                                                  \
              .group_id(share::OBCG_CLOG)                                                                     \
              .post_packet(pkt, &cb);                                                                         \
    return ret;                                                                                               \
  }
// ELECTION use unique message queue
#define DEFINE_RPC_PROXY_ELECTION_POST_FUNCTION(REQTYPE, PCODE)                                               \
  int LogRpcProxyV2::post_packet(const common::ObAddr &dst, const palf::LogRpcPacketImpl<palf::REQTYPE> &pkt, \
                                 const int64_t tenant_id)                                                     \
  {                                                                                                           \
    int ret = common::OB_SUCCESS;                                                                             \
    static obrpc::LogRpcCB<obrpc::PCODE> cb;                                                                  \
    ret = this->to(dst)                                                                                       \
              .timeout(3000 * 1000)                                                                           \
              .trace_time(true)                                                                               \
              .max_process_handler_time(100 * 1000)                                                           \
              .by(tenant_id)                                                                                  \
              .group_id(share::OBCG_ELECTION)                                                                    \
              .post_packet(pkt, &cb);                                                                         \
    return ret;                                                                                               \
  }

#define DECLARE_SYNC_RPC_PROXY_POST_FUNCTION(PRIO, NAME, REQTYPE, RESPTYPE, PCODE)                          \
  RPC_S(PRIO NAME, PCODE, (palf::LogRpcPacketImpl<palf::REQTYPE>), palf::LogRpcPacketImpl<palf::RESPTYPE>); \
  int post_sync_packet(const common::ObAddr &dst, const int64_t tenant_id, const int64_t timeout_us,        \
                       const palf::LogRpcPacketImpl<palf::REQTYPE> &pkt, palf::LogRpcPacketImpl<palf::RESPTYPE> &resp)

#define DEFINE_SYNC_RPC_PROXY_POST_FUNCTION(NAME, REQTYPE, RESPTYPE)                                                \
  int LogRpcProxyV2::post_sync_packet(const common::ObAddr &dst, const int64_t tenant_id, const int64_t timeout_us, \
                                      const palf::LogRpcPacketImpl<palf::REQTYPE> &pkt,                             \
                                      palf::LogRpcPacketImpl<palf::RESPTYPE> &resp)                                 \
  {                                                                                                                 \
    int ret = common::OB_SUCCESS;                                                                                   \
    ret = this->to(dst)                                                                                             \
              .timeout(timeout_us)                                                                                  \
              .trace_time(true)                                                                                     \
              .max_process_handler_time(100 * 1000)                                                                 \
              .by(tenant_id)                                                                                        \
              .group_id(share::OBCG_CLOG)                                                                           \
              .NAME(pkt, resp);                                                                                     \
    return ret;                                                                                                     \
  }

#define DEFINE_SYNC_RPC_PROCESSOR(CLASS, PROXY, REQTYPE, RESPTYPE, PCODE)                                         \
  class CLASS : public obrpc::ObRpcProcessor<PROXY::ObRpc<PCODE>>                                                 \
  {                                                                                                               \
  public:                                                                                                         \
    CLASS() : palf_env_impl_(NULL), filter_(NULL) {}                                                              \
    virtual ~CLASS() {}                                                                                           \
    int process()                                                                                                 \
    {                                                                                                             \
      int ret = OB_SUCCESS;                                                                                       \
      LogRpcPacketImpl<REQTYPE> &rpc_packet = arg_;                                                               \
      const REQTYPE &req = rpc_packet.req_;                                                                       \
      const ObAddr server = rpc_packet.src_;                                                                      \
      int64_t palf_id = rpc_packet.palf_id_;                                                                      \
      RESPTYPE &resp = result_.req_;                                                                              \
      result_.palf_id_ = palf_id;                                                                                 \
      if (OB_ISNULL(palf_env_impl_) && OB_FAIL(__get_palf_env_impl(rpc_pkt_->get_tenant_id(), palf_env_impl_))) { \
        PALF_LOG(WARN, "__get_palf_env_impl failed", K(ret), KPC(rpc_pkt_));                                      \
      } else if (NULL != filter_ && true == (*filter_)(server)) {                                                 \
        PALF_LOG(INFO, "need filter this packet", K(rpc_packet));                                                 \
      } else {                                                                                                    \
        LogRequestHandler handler(palf_env_impl_);                                                                \
        ret = handler.handle_sync_request(palf_id, server, req, resp);                                            \
      }                                                                                                           \
      return ret;                                                                                                 \
    }                                                                                                             \
    void set_palf_env_impl(void *palf_env_impl, void *filter)                                                     \
    {                                                                                                             \
      filter_ = reinterpret_cast<ObFunction<bool(const ObAddr &src)> *>(filter);                                  \
      palf_env_impl_ = reinterpret_cast<PalfEnvImpl *>(palf_env_impl);                                            \
    }                                                                                                             \
                                                                                                                  \
  private:                                                                                                        \
    PalfEnvImpl *palf_env_impl_;                                                                                  \
    ObFunction<bool(const ObAddr &src)> *filter_;                                                                 \
  }

#endif // for OCEANBASE_LOGSERVICE_LOG_RPC_MACROS_
