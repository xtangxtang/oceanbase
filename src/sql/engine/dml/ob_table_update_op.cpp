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

#define USING_LOG_PREFIX SQL_ENG

#include "ob_table_update_op.h"
#include "share/system_variable/ob_system_variable.h"
#include "sql/engine/dml/ob_dml_service.h"
#include "sql/engine/dml/ob_trigger_handler.h"
#include "sql/engine/expr/ob_expr_calc_partition_id.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace storage;
namespace sql
{


OB_SERIALIZE_MEMBER((ObTableUpdateOpInput, ObTableModifyOpInput));

ObTableUpdateSpec::ObTableUpdateSpec(ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObTableModifySpec(alloc, type),
      alloc_(alloc)
{
}

ObTableUpdateSpec::~ObTableUpdateSpec()
{
}

OB_DEF_SERIALIZE(ObTableUpdateSpec)
{
  int ret = OB_SUCCESS;
  int64_t tbl_cnt = upd_ctdefs_.count();
  BASE_SER((ObTableUpdateSpec, ObTableModifySpec));
  OB_UNIS_ENCODE(tbl_cnt);
  for (int64_t i = 0; OB_SUCC(ret) && i < tbl_cnt; ++i) {
    int64_t idx_cnt = upd_ctdefs_.at(i).count();
    OB_UNIS_ENCODE(idx_cnt);
    for (int64_t j = 0; OB_SUCC(ret) && j < idx_cnt; ++j) {
      ObUpdCtDef *upd_ctdef = upd_ctdefs_.at(i).at(j);
      if (OB_ISNULL(upd_ctdef)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("upd_ctdef is nullptr", K(ret));
      }
      OB_UNIS_ENCODE(*upd_ctdef);
    }
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObTableUpdateSpec)
{
  int ret = OB_SUCCESS;
  int64_t tbl_cnt = 0;
  BASE_DESER((ObTableUpdateSpec, ObTableModifySpec));
  OB_UNIS_DECODE(tbl_cnt);
  if (OB_SUCC(ret) && tbl_cnt > 0) {
    OZ(upd_ctdefs_.allocate_array(alloc_, tbl_cnt));
  }
  ObDMLCtDefAllocator<ObUpdCtDef> ctdef_allocator(alloc_);
  for (int64_t i = 0; OB_SUCC(ret) && i < tbl_cnt; ++i) {
    int64_t index_cnt = 0;
    OB_UNIS_DECODE(index_cnt);
    OZ(upd_ctdefs_.at(i).allocate_array(alloc_, index_cnt));
    for (int64_t j = 0; OB_SUCC(ret) && j < index_cnt; ++j) {
      ObUpdCtDef *upd_ctdef = ctdef_allocator.alloc();
      if (OB_ISNULL(upd_ctdef)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc upd_ctdef failed", K(ret));
      }
      OB_UNIS_DECODE(*upd_ctdef);
      upd_ctdefs_.at(i).at(j) = upd_ctdef;
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObTableUpdateSpec)
{
  int64_t len = 0;
  int64_t tbl_cnt = upd_ctdefs_.count();
  BASE_ADD_LEN((ObTableUpdateSpec, ObTableModifySpec));
  OB_UNIS_ADD_LEN(tbl_cnt);
  for (int64_t i = 0; i < tbl_cnt; ++i) {
    int64_t index_cnt = upd_ctdefs_.at(i).count();
    OB_UNIS_ADD_LEN(index_cnt);
    for (int64_t j = 0; j < index_cnt; ++j) {
      ObUpdCtDef *upd_ctdef = upd_ctdefs_.at(i).at(j);
      if (upd_ctdef != nullptr) {
        OB_UNIS_ADD_LEN(*upd_ctdef);
      }
    }
  }
  return len;
}

ObTableUpdateOp::ObTableUpdateOp(ObExecContext &exec_ctx, const ObOpSpec &spec,
                                 ObOpInput *input)
  : ObTableModifyOp(exec_ctx, spec, input),
    err_log_service_(ObOperator::get_eval_ctx())
{
}

int ObTableUpdateOp::inner_open()
{
  int ret = OB_SUCCESS;
  NG_TRACE(update_open);
  //execute update with das
  if (OB_FAIL(ObTableModifyOp::inner_open())) {
    LOG_WARN("open child operator failed", K(ret));
  } else if (OB_UNLIKELY(MY_SPEC.upd_ctdefs_.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("del ctdef is invalid", K(ret), KP(this));
  } else if (OB_UNLIKELY(iter_end_)) {
    //do nothing
  } else if (OB_FAIL(inner_open_with_das())) {
    LOG_WARN("inner open with das failed", K(ret));
  }
  NG_TRACE(update_end);
  return ret;
}

int ObTableUpdateOp::inner_close()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(close_table_for_each())) {
    LOG_WARN("close table for each", K(ret));
  }
  int  close_ret = ObTableModifyOp::inner_close();
  return (OB_SUCCESS == ret) ? close_ret : ret;
}

int ObTableUpdateOp::inner_rescan()
{
  int ret = OB_SUCCESS;
  if (!MY_SPEC.gi_above_) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("table update rescan not supported", K(ret));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "table update rescan");
  } else if (OB_FAIL(ObTableModifyOp::inner_rescan())) {
    LOG_WARN("rescan child operator failed", K(ret));
  } else if (OB_UNLIKELY(iter_end_)) {
    //do nothing
  } else if (OB_FAIL(close_table_for_each())) {
    LOG_WARN("close table for each failed", K(ret));
  } else if (OB_FAIL(open_table_for_each())) {
    LOG_WARN("open table for each failed", K(ret));
  }
  return ret;
}

int ObTableUpdateOp::inner_switch_iterator()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObTableModifyOp::inner_switch_iterator())) {
    LOG_WARN("switch single child operator iterator failed", K(ret));
  }
  return ret;
}

int ObTableUpdateOp::do_instead_of_trigger_update()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = GET_PHY_PLAN_CTX(ctx_);
  for (int64_t i = 0; OB_SUCC(ret) && i < MY_SPEC.upd_ctdefs_.count(); ++i) {
    const ObTableUpdateSpec::UpdCtDefArray &ctdefs = MY_SPEC.upd_ctdefs_.at(i);
    if (0 < upd_rtdefs_.count()) {
      // pdml upd_rtdefs_ maybe empty
      UpdRtDefArray &rtdefs = upd_rtdefs_.at(i);
      for (int64_t j = 0; OB_SUCC(ret) && j < ctdefs.count(); ++j) {
        const ObUpdCtDef &upd_ctdef = *ctdefs.at(j);
        ObUpdRtDef &upd_rtdef = rtdefs.at(j);
        if (upd_ctdef.is_primary_index_) {
          if (OB_FAIL(ObDMLService::process_instead_of_trigger_update(upd_ctdef, upd_rtdef, *this))) {
            LOG_WARN("failed to process instead of trigger", K(ret));
          } else {
            plan_ctx->add_affected_rows(1);
            plan_ctx->add_row_matched_count(1);
          }
        }
      }
    }
  }
  return ret;
}

int ObTableUpdateOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  if (iter_end_) {
    LOG_DEBUG("can't get gi task, iter end", K(MY_SPEC.id_));
    ret = OB_ITER_END;
  } else {
    while (OB_SUCC(ret)) {
      if (OB_FAIL(try_check_status())) {
        LOG_WARN("check status failed", K(ret));
      } else if (OB_FAIL(get_next_row_from_child())) {
        if (OB_ITER_END != ret) {
          LOG_WARN("fail to get next row", K(ret));
        } else {
          iter_end_ = true;
        }
      } else if (MY_SPEC.has_instead_of_trigger_) {
        if (OB_FAIL(do_instead_of_trigger_update())) {
          LOG_WARN("failed to do instead of trigger", K(ret));
        }
      } else if (OB_FAIL(update_row_to_das())) {
        LOG_WARN("update row to das failed", K(ret));
      } else if (is_error_logging_ && err_log_rt_def_.first_err_ret_ != OB_SUCCESS) {
        clear_evaluated_flag();
        err_log_rt_def_.curr_err_log_record_num_++;
        err_log_rt_def_.reset();
        continue;
      } else if (MY_SPEC.is_returning_) {
        break;
      }
    }
    if (OB_ITER_END == ret) {
      if (!MY_SPEC.has_instead_of_trigger_ && OB_FAIL(upd_rows_post_proc())) {
        LOG_WARN("do update rows post process failed", K(ret));
      } else {
        //can not overwrite the original error code
        ret = OB_ITER_END;
      }
    }
  }
  return ret;
}

OB_INLINE int ObTableUpdateOp::inner_open_with_das()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(open_table_for_each())) {
    LOG_WARN("init update rtdef failed", K(ret), K(MY_SPEC.upd_ctdefs_.count()));
  } else {
    dml_rtctx_.set_pick_del_task_first();
  }
  return ret;
}

OB_INLINE int ObTableUpdateOp::open_table_for_each()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(upd_rtdefs_.allocate_array(ctx_.get_allocator(), MY_SPEC.upd_ctdefs_.count()))) {
    LOG_WARN("allocate update rtdef failed", K(ret), K(MY_SPEC.upd_ctdefs_.count()));
  }
  trigger_clear_exprs_.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < upd_rtdefs_.count(); ++i) {
    UpdRtDefArray &rtdefs = upd_rtdefs_.at(i);
    const ObTableUpdateSpec::UpdCtDefArray &ctdefs = MY_SPEC.upd_ctdefs_.at(i);
    if (OB_FAIL(rtdefs.allocate_array(ctx_.get_allocator(), ctdefs.count()))) {
      LOG_WARN("allocate update rtdefs failed", K(ret), K(ctdefs.count()));
    }
    for (int64_t j = 0; OB_SUCC(ret) && j < rtdefs.count(); ++j) {
      const ObUpdCtDef &upd_ctdef = *ctdefs.at(j);
      ObUpdRtDef &upd_rtdef = rtdefs.at(j);
      upd_rtdef.primary_rtdef_ = &rtdefs.at(0);
      if (OB_FAIL(ObDMLService::init_upd_rtdef(dml_rtctx_, upd_rtdef, upd_ctdef, trigger_clear_exprs_))) {
        LOG_WARN("init upd rtdef failed", K(ret));
      }
    }
    if (OB_SUCC(ret) && !rtdefs.empty()) {
      const ObUpdCtDef &primary_upd_ctdef = *ctdefs.at(0);
      ObUpdRtDef &primary_upd_rtdef = rtdefs.at(0);
      if (primary_upd_ctdef.error_logging_ctdef_.is_error_logging_) {
        is_error_logging_ = true;
      }
      if (OB_FAIL(ObDMLService::process_before_stmt_trigger(primary_upd_ctdef,
                                                            primary_upd_rtdef,
                                                            dml_rtctx_,
                                                            ObDmlEventType::DE_UPDATING))) {
        LOG_WARN("process before stmt trigger failed", K(ret));
      } else {
        //this table is being accessed by dml operator, mark its table location as writing
        primary_upd_rtdef.dupd_rtdef_.table_loc_->is_writing_ = true;
      }
    }
  }
  return ret;
}

OB_INLINE int ObTableUpdateOp::close_table_for_each()
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < upd_rtdefs_.count(); ++i) {
    if (!upd_rtdefs_.at(i).empty()) {
      const ObUpdCtDef &primary_upd_ctdef = *MY_SPEC.upd_ctdefs_.at(i).at(0);
      ObUpdRtDef &primary_upd_rtdef = upd_rtdefs_.at(i).at(0);
      if (OB_FAIL(ObDMLService::process_after_stmt_trigger(primary_upd_ctdef,
                                                           primary_upd_rtdef,
                                                           dml_rtctx_,
                                                           ObDmlEventType::DE_UPDATING))) {
        LOG_WARN("process after stmt trigger failed", K(ret));
      }
    }
  }
  //whether it is successful or not, needs to release rtdef
  for (int64_t i = 0; i < upd_rtdefs_.count(); ++i) {
    upd_rtdefs_.at(i).release_array();
  }
  return ret;
}

OB_INLINE int ObTableUpdateOp::calc_multi_tablet_id(const ObUpdCtDef &upd_ctdef,
                                                    ObExpr &part_id_expr,
                                                    ObTabletID &tablet_id,
                                                    bool check_exist)
{
  int ret = OB_SUCCESS;
  ObObjectID partition_id = OB_INVALID_ID;
  if (OB_FAIL(ObExprCalcPartitionBase::calc_part_and_tablet_id(&part_id_expr,
                                                               eval_ctx_,
                                                               partition_id,
                                                               tablet_id))) {
    LOG_WARN("extract part and tablet id failed", K(ret));
  } else if (check_exist && (!upd_ctdef.multi_ctdef_->hint_part_ids_.empty()
                             && !has_exist_in_array(
                               upd_ctdef.multi_ctdef_->hint_part_ids_, partition_id))) {
    ret = OB_PARTITION_NOT_MATCH;
    LOG_WARN("Partition not match",
             K(ret), K(partition_id), K(upd_ctdef.multi_ctdef_->hint_part_ids_));
  }
  return ret;
}

OB_INLINE int ObTableUpdateOp::calc_tablet_loc(const ObUpdCtDef &upd_ctdef,
                                               ObUpdRtDef &upd_rtdef,
                                               ObDASTabletLoc *&old_tablet_loc,
                                               ObDASTabletLoc *&new_tablet_loc)
{
  int ret = OB_SUCCESS;
  if (MY_SPEC.use_dist_das_) {
    ObTabletID old_tablet_id;
    ObTabletID new_tablet_id;
    if (upd_ctdef.multi_ctdef_ != nullptr) {
      ObExpr *calc_part_id_old = upd_ctdef.multi_ctdef_->calc_part_id_old_;
      ObExpr *calc_part_id_new = upd_ctdef.multi_ctdef_->calc_part_id_new_;
      if (calc_part_id_old != nullptr) {
        if (OB_FAIL(calc_multi_tablet_id(upd_ctdef, *calc_part_id_old, old_tablet_id))) {
          LOG_WARN("calc multi old part key failed", K(ret));
        } else if (OB_FAIL(calc_multi_tablet_id(upd_ctdef, *calc_part_id_new, new_tablet_id, true))) {
          LOG_WARN("calc multi new part key failed", K(ret));
        }
      }
    }
    if (OB_SUCC(ret)) {
      ObDASTableLoc &table_loc = *upd_rtdef.dupd_rtdef_.table_loc_;
      if (old_tablet_id == new_tablet_id) {
        if (OB_FAIL(DAS_CTX(ctx_).extended_tablet_loc(table_loc, old_tablet_id, old_tablet_loc))) {
          LOG_WARN("extended old row tablet loc failed", K(ret), K(old_tablet_id));
        } else {
          new_tablet_loc = old_tablet_loc;
        }
      } else if (OB_FAIL(DAS_CTX(ctx_).extended_tablet_loc(table_loc, old_tablet_id, old_tablet_loc))) {
        LOG_WARN("extended old tablet location failed", K(ret), K(old_tablet_id));
      } else if (OB_FAIL(DAS_CTX(ctx_).extended_tablet_loc(table_loc, new_tablet_id, new_tablet_loc))) {
        LOG_WARN("extended new tablet location failed", K(ret), K(new_tablet_id));
      }
    }
  } else {
    //direct write delete row to storage
    old_tablet_loc = MY_INPUT.get_tablet_loc();
    new_tablet_loc = old_tablet_loc;
  }
  return ret;
}

OB_INLINE int ObTableUpdateOp::update_row_to_das()
{
  int ret = OB_SUCCESS;

  for (int64_t i = 0; OB_SUCC(ret) && i < MY_SPEC.upd_ctdefs_.count(); ++i) {
    const ObTableUpdateSpec::UpdCtDefArray &ctdefs = MY_SPEC.upd_ctdefs_.at(i);
    UpdRtDefArray &rtdefs = upd_rtdefs_.at(i);
    for (int64_t j = 0; OB_SUCC(ret) && j < ctdefs.count(); ++j) {
      //update each table with fetched row
      const ObUpdCtDef &upd_ctdef = *ctdefs.at(j);
      ObUpdRtDef &upd_rtdef = rtdefs.at(j);
      ObDASTabletLoc *old_tablet_loc = nullptr;
      ObDASTabletLoc *new_tablet_loc = nullptr;
      bool is_skipped = false;
      ++upd_rtdef.cur_row_num_;
      if (OB_FAIL(ObDMLService::process_update_row(upd_ctdef, upd_rtdef, is_skipped, *this))) {
        LOG_WARN("process update row failed", K(ret));
      } else if (OB_UNLIKELY(is_skipped)) {
        //this row has been skipped, so can not write to DAS buffer(include its global index)
        //so need to break this loop
        break;
      } else if (OB_FAIL(calc_tablet_loc(upd_ctdef, upd_rtdef, old_tablet_loc, new_tablet_loc))) {
        LOG_WARN("calc partition key failed", K(ret));
      } else if (OB_FAIL(TriggerHandle::do_handle_after_row(
          *this, upd_ctdef.trig_ctdef_, upd_rtdef.trig_rtdef_, ObTriggerEvents::get_update_event()))) {
        LOG_WARN("failed to handle after trigger", K(ret));
      } else if (OB_FAIL(ObDMLService::update_row(upd_ctdef, upd_rtdef, old_tablet_loc, new_tablet_loc, dml_rtctx_))) {
        LOG_WARN("insert row with das failed", K(ret));
      } else {
        ++upd_rtdef.found_rows_;
      }
    } //end for global index ctdef loop
    if (OB_SUCC(ret)) {
      int64_t update_rows = rtdefs.at(0).is_row_changed_ ? 1 : 0;
      if (OB_FAIL(merge_implict_cursor(0, update_rows, 0, 1))) {
        LOG_WARN("merge implict cursor failed", K(ret));
      }
    }
  } // end for table ctdef loop

  if (is_error_logging_ &&
      OB_SUCCESS != err_log_rt_def_.first_err_ret_ &&
      should_catch_err(ret)) {
    if (OB_FAIL(err_log_service_.insert_err_log_record(GET_MY_SESSION(ctx_),
                                                       MY_SPEC.upd_ctdefs_.at(0).at(0)->error_logging_ctdef_,
                                                       err_log_rt_def_,
                                                       ObDASOpType::DAS_OP_TABLE_UPDATE))) {
      LOG_WARN("fail to insert_err_log_record", K(ret));
    }
  }
  return ret;
}

int ObTableUpdateOp::check_update_affected_row()
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < upd_rtdefs_.count(); ++i) {
    UpdRtDefArray &upd_rtdef_array = upd_rtdefs_.at(i);
    const ObUpdCtDef *primary_upd_ctdef = MY_SPEC.upd_ctdefs_.at(i).at(0);
    ObUpdRtDef &primary_upd_rtdef = upd_rtdef_array.at(0);
    int64_t primary_write_rows = 0;
    primary_write_rows += primary_upd_rtdef.dupd_rtdef_.affected_rows_;
    if (primary_upd_rtdef.ddel_rtdef_ != nullptr) {
      //update rows across partitions, need to add das delete op's affected rows
      primary_write_rows += primary_upd_rtdef.ddel_rtdef_->affected_rows_;
    }
    if (primary_upd_rtdef.dlock_rtdef_ != nullptr) {
      primary_write_rows += primary_upd_rtdef.dlock_rtdef_->affected_rows_;
    }
    for (int64_t j = 1; OB_SUCC(ret) && j < upd_rtdef_array.count(); ++j) {
      int64_t index_write_rows = 0;
      const ObUpdCtDef *index_upd_ctdef = MY_SPEC.upd_ctdefs_.at(i).at(j);
      ObUpdRtDef &index_upd_rtdef = upd_rtdef_array.at(j);
      index_write_rows += index_upd_rtdef.dupd_rtdef_.affected_rows_;
      if (index_upd_rtdef.ddel_rtdef_ != nullptr) {
        //update rows across partitions, need to add das delete op's affected rows
        index_write_rows += index_upd_rtdef.ddel_rtdef_->affected_rows_;
      }
      if (index_upd_rtdef.dlock_rtdef_ != nullptr) {
        index_write_rows += index_upd_rtdef.dlock_rtdef_->affected_rows_;
      }
      if (primary_write_rows != index_write_rows) {
        ret = OB_ERR_DEFENSIVE_CHECK;
        ObString func_name = ObString::make_string("check_update_affected_row");
        LOG_USER_ERROR(OB_ERR_DEFENSIVE_CHECK, func_name.length(), func_name.ptr());
        LOG_ERROR("Fatal Error!!! data table update affected row is not match with index table",
                  K(ret), K(primary_write_rows), K(index_write_rows),
                  KPC(primary_upd_ctdef), K(primary_upd_rtdef),
                  KPC(index_upd_ctdef), K(index_upd_rtdef));
      }
    }
    if (OB_SUCC(ret) && !primary_upd_ctdef->dupd_ctdef_.is_ignore_) {
      if (primary_upd_rtdef.found_rows_ != primary_write_rows) {
        ret = OB_ERR_DEFENSIVE_CHECK;
        ObString func_name = ObString::make_string("check_update_affected_row");
        LOG_USER_ERROR(OB_ERR_DEFENSIVE_CHECK, func_name.length(), func_name.ptr());
        LOG_ERROR("Fatal Error!!! data table update affected row is not match with found rows",
                  K(ret), K(primary_write_rows), K(primary_upd_rtdef.found_rows_),
                  KPC(primary_upd_ctdef), K(primary_upd_rtdef));
      }
    }
  }
  return ret;
}

OB_INLINE int ObTableUpdateOp::upd_rows_post_proc()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = GET_PHY_PLAN_CTX(ctx_);
  ObSQLSessionInfo *session = GET_MY_SESSION(ctx_);
  int64_t found_rows = 0;
  int64_t changed_rows = 0;
  //iterator end, if das ref has task, need flush all task data to partition storage
  if (dml_rtctx_.das_ref_.has_task()) {
    if (OB_FAIL(dml_rtctx_.das_ref_.pick_del_task_to_first())) {
      LOG_WARN("pick delete das task to first failed", K(ret));
    } else if (OB_FAIL(submit_all_dml_task())) {
      LOG_WARN("execute all update das task failed", K(ret));
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < upd_rtdefs_.count(); ++i) {
    ObUpdRtDef &upd_rtdef = upd_rtdefs_.at(i).at(0);
    found_rows += upd_rtdef.found_rows_;
    changed_rows += upd_rtdef.dupd_rtdef_.affected_rows_;
    if (upd_rtdef.ddel_rtdef_ != nullptr) {
      //update rows across partitions, need to add das delete op's affected rows
      changed_rows += upd_rtdef.ddel_rtdef_->affected_rows_;
      //insert new row to das after old row has been deleted in storage
      //reference to: https://work.aone.alibaba-inc.com/issue/31915604
    }
    LOG_DEBUG("update rows post proc", K(ret), K(found_rows), K(changed_rows), K(upd_rtdef));
  }
  if (OB_SUCC(ret)) {
    plan_ctx->add_row_matched_count(found_rows);
    plan_ctx->add_row_duplicated_count(changed_rows);
    plan_ctx->add_affected_rows(session->get_capability().cap_flags_.OB_CLIENT_FOUND_ROWS ?
                                found_rows : changed_rows);
  }
  if (OB_SUCC(ret) && GCONF.enable_defensive_check()) {
    if (OB_FAIL(check_update_affected_row())) {
      LOG_WARN("check index upd consistency failed", K(ret));
    }
  }

  return ret;
}

OB_INLINE int ObTableUpdateOp::get_next_row_from_child()
{
  int ret = OB_SUCCESS;
  clear_evaluated_flag();
  if (OB_FAIL(child_->get_next_row())) {
    if (OB_ITER_END != ret) {
      LOG_WARN("fail to get next row", K(ret));
    }
  } else {
    LOG_TRACE("child output row", "row", ROWEXPR2STR(eval_ctx_, child_->get_spec().output_));
  }
  return ret;
}
} // end namespace sql
} // end namespace oceanbase
