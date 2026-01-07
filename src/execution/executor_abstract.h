/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include "execution_defs.h"
#include "common/common.h"
#include "index/ix.h"
#include "system/sm.h"

class AbstractExecutor {
   public:
    Rid _abstract_rid;

    Context *context_;

    virtual ~AbstractExecutor() = default;

    virtual size_t tupleLen() const { return 0; };

    virtual const std::vector<ColMeta> &cols() const {
        std::vector<ColMeta> *_cols = nullptr;
        return *_cols;
    };

    virtual std::string getType() { return "AbstractExecutor"; }; //have added to all

    virtual void beginTuple(){};

    virtual void nextTuple(){};

    virtual bool is_end() const { return true; };

    virtual Rid &rid() = 0;

    virtual std::unique_ptr<RmRecord> Next() = 0;

    virtual ColMeta get_col_offset(const TabCol &target) { return ColMeta();};

    std::vector<ColMeta>::const_iterator get_col(const std::vector<ColMeta> &rec_cols, const TabCol &target) {
        auto pos = std::find_if(rec_cols.begin(), rec_cols.end(), [&](const ColMeta &col) {
            return col.tab_name == target.tab_name && col.name == target.col_name;
        });
        if (pos == rec_cols.end()) {
            throw ColumnNotFoundError(target.tab_name + '.' + target.col_name);
        }
        return pos;
    }

    int ex_compare(const char *a, const char *b, ColType type, int col_len) {
        switch (type) {
            case TYPE_INT: {
                int ia; memcpy(&ia, a, sizeof(int)); //这里没有确定是不是对齐的
                int ib; memcpy(&ib, b, sizeof(int));
                return (ia < ib) ? -1 : ((ia > ib) ? 1 : 0);
            }
            case TYPE_FLOAT: {
                float fa; memcpy(&fa, a, sizeof(float));
                float fb; memcpy(&fb, b, sizeof(float));
                return (fa < fb) ? -1 : ((fa > fb) ? 1 : 0);
            }
            case TYPE_STRING:
                return memcmp(a, b, col_len);
            default:
                throw InternalError("Unexpected data type");
        }
    }


    bool satisfy(std::unique_ptr<RmRecord> rec, std::vector<ColMeta> &cols, std::vector<Condition> &conds) {
        for (auto cond : conds) {
            auto it = get_col(cols, cond.lhs_col);
            ColType type = (*it).type;
            char *a = rec->data + (*it).offset;
            char *b;
            if (cond.is_rhs_val) {
                switch(cond.rhs_val.type) {
                    case ColType::TYPE_INT :
                        b = reinterpret_cast<char*>(&cond.rhs_val.int_val);
                        break;
                    case ColType::TYPE_FLOAT :
                        b = reinterpret_cast<char*>(&cond.rhs_val.float_val);
                        break;
                    case ColType::TYPE_STRING :
                        b = cond.rhs_val.str_val.data();
                        break;
                }
            }
            else {
                b = rec->data + (*get_col(cols, cond.rhs_col)).offset;
            }
            int cmp = ex_compare(a, b, type, (*it).len);
            bool ok;
            switch (cond.op) {
                case CompOp::OP_EQ :
                    ok = cmp == 0;
                    break;
                case CompOp::OP_GE :
                    ok = cmp >= 0;
                    break;
                case CompOp::OP_GT :
                    ok = cmp == 1;
                    break;
                case CompOp::OP_LE :
                    ok = cmp <= 0;
                    break;
                case CompOp::OP_LT :
                    ok = cmp == -1;
                    break;
                case CompOp::OP_NE :
                    ok = cmp != 0;
                    break;
            }
            if (!ok) return false;
        }
        return true;
    }

    void fill_buf(IndexMeta &index, char *rec, char *buf) {
        int offset = 0;
        for (auto &col : index.cols) {
            memcpy(buf + offset, rec + col.offset, col.len);
            offset += col.len;
        }
    }

    void abort(Transaction * txn, LogManager *log_manager, SmManager *sm_manager_, LockManager *lock_manager_) {
        // Todo:
        // 1. 回滚所有写操作
        // 2. 释放所有锁
        // 3. 清空事务相关资源，eg.锁集
        // 4. 把事务日志刷入磁盘中
        // 5. 更新事务状态

        auto write_set = txn->get_write_set();

        while (write_set->size()) {
            auto write_record = write_set->back(); 
            auto rid = write_record->GetRid();
            write_set->pop_back();

            if (write_record->GetWriteType() == WType::INSERT_TUPLE) {
                sm_manager_->fhs_[write_record->GetTableName()]->delete_record(rid, nullptr);
            }
            else if (write_record->GetWriteType() == WType::DELETE_TUPLE) {
                auto record = write_record->GetRecord();
                sm_manager_->fhs_[write_record->GetTableName()]->insert_record(rid, record.data);
            }
            else if (write_record->GetWriteType() == WType::UPDATE_TUPLE) {
                auto record = write_record->GetRecord();
                sm_manager_->fhs_[write_record->GetTableName()]->update_record(rid, record.data, nullptr);
            }
        }
        txn->set_state(TransactionState::ABORTED);
        lock_manager_->unlock(txn);
    }
};