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

#define USING_LOG_PREFIX SHARE

#include "lib/oblog/ob_log.h"
#include "lib/thread/thread_mgr.h"
#include "share/ob_thread_mgr.h"
#include "share/ash/ob_active_sess_hist_task.h"
#include "share/ash/ob_active_sess_hist_list.h"
#include "sql/session/ob_sql_session_mgr.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::sql;

ObActiveSessHistTask &ObActiveSessHistTask::get_instance()
{
  static ObActiveSessHistTask the_one;
  return the_one;
}

int ObActiveSessHistTask::init()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObActiveSessHistTask::start()
{
  int ret = OB_SUCCESS;
  // refresh sess info every 1 second
  const static int64_t REFRESH_INTERVAL = 1 * 1000L * 1000L;
  if (OB_FAIL(TG_START(lib::TGDefIDs::ActiveSessHist))) {
    LOG_WARN("fail to init timer", K(ret));
  } else if (OB_FAIL(TG_SCHEDULE(lib::TGDefIDs::ActiveSessHist,
                                 *this,
                                 REFRESH_INTERVAL,
                                 true /* repeat */))) {
    LOG_WARN("fail define timer schedule", K(ret));
  } else {
    LOG_INFO("ASH init OK");
  }
  return ret;
}

void ObActiveSessHistTask::wait()
{
  TG_WAIT(lib::TGDefIDs::ActiveSessHist);
}

void ObActiveSessHistTask::stop()
{
  TG_STOP(lib::TGDefIDs::ActiveSessHist);
}

void ObActiveSessHistTask::destroy()
{
  TG_DESTROY(lib::TGDefIDs::ActiveSessHist);
}

void ObActiveSessHistTask::runTimerTask()
{
  if (OB_NOT_NULL(GCTX.session_mgr_)) {
    sample_time_ = ObTimeUtility::current_time();
    GCTX.session_mgr_->for_each_session(*this);
  }
}

bool ObActiveSessHistTask::operator()(sql::ObSQLSessionMgr::Key key, ObSQLSessionInfo *sess_info)
{
  if (OB_ISNULL(sess_info)) {
  } else if (ObSQLSessionState::QUERY_ACTIVE == sess_info->get_session_state()) {
    ActiveSessionStat &stat = sess_info->get_ash_stat();
    stat.sample_time_ = sample_time_;
    stat.tenant_id_ = sess_info->get_effective_tenant_id();
    stat.user_id_ = sess_info->get_user_id();
    stat.session_id_ = sess_info->get_sessid();
    stat.trace_id_ = sess_info->get_current_trace_id();
    sess_info->get_cur_sql_id(stat.sql_id_, sizeof(stat.sql_id_));
    ObActiveSessHistList::get_instance().add(stat);
  }
  return true;
}

