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
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

const bool upd_ex_debug = false;

class UpdateExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;
    std::vector<Condition> conds_;
    RmFileHandle *fh_;
    std::vector<Rid> rids_;
    std::string tab_name_;
    std::vector<SetClause> set_clauses_;
    SmManager *sm_manager_;
    RmRecord *rec_;

   public:
    UpdateExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<SetClause> set_clauses,
                   std::vector<Condition> conds, std::vector<Rid> rids, Context *context, RmRecord *rec = nullptr) {
        sm_manager_ = sm_manager;
        tab_name_ = tab_name;
        set_clauses_ = set_clauses;
        tab_ = sm_manager_->db_.get_table(tab_name);
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        conds_ = conds;
        rids_ = rids;
        context_ = context; 
        rec_ = rec;
    }
    ~UpdateExecutor() override {}

    std::string getType() override { return "UpdateExecutor"; };

    void fill_buf(IndexMeta &index, char *rec, char *buf) {
        int offset = 0;
        for (auto &col : index.cols) {
            memcpy(buf + offset, rec + col.offset, col.len);
            offset += col.len;
        }
    }
    
    std::unique_ptr<RmRecord> Next() override {
        if (upd_ex_debug) {
            std::fstream outfile;
            outfile.open("output.txt",std::ios::out | std::ios::app);
            outfile << "update\n";
            for (auto &clause : set_clauses_) {
                outfile << clause.lhs.col_name << "\n";
            }
            outfile.close();
        }
        
        std::vector<int> len_vec, offset_vec;
        for (auto &clause : set_clauses_) {
            auto it = tab_.get_col(clause.lhs.col_name);
            int len = (*it).len;
            len_vec.push_back(len);
            if (clause.rhs.raw != nullptr) clause.rhs.raw.reset();
            clause.rhs.init_raw(len);
            offset_vec.push_back((*it).offset);
        }

        auto buf = std::make_unique<char[]>(fh_->get_record_size());

        for (auto rid : rids_) {
        // auto scan_ = std::make_unique<RmScan>(fh_);
        // while (!scan_->is_end()) {
            // Rid rid = scan_->rid(); scan_->next();
            // if (!satisfy(fh_->get_record(rid, context_), tab_.cols, conds_)) continue;

            if (!context_->lock_mgr_->lock_exclusive_on_record(context_->txn_, rid, fh_->GetFd()) ||
                !context_->lock_mgr_->lock_IX_on_table(context_->txn_, fh_->GetFd())) {
                throw TransactionAbortException(context_->txn_->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
            }

            auto ori_rec = *fh_->get_record(rid, context_);
            auto rec = ori_rec;

            //更新record
            if (rec_ == nullptr) {
                int i = 0;
                for (auto &clause : set_clauses_) {
                    memcpy(rec.data + offset_vec[i], clause.rhs.raw->data, len_vec[i]);
                    i++;
                }
            }
            else {
                rec = *rec_;
            }
            fh_->update_record(rid, rec.data, context_);

            //更新索引
            for (auto &index : tab_.indexes) {
                auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                fill_buf(index, ori_rec.data, buf.get());
                ih->delete_entry(buf.get(), nullptr);

                fill_buf(index, rec.data, buf.get());
                ih->insert_entry(buf.get(), rid, nullptr);
            }
            if (context_->txn_ && context_->txn_->get_state() != TransactionState::ABORTED) {
                context_->txn_->append_write_record(new WriteRecord(WType::UPDATE_TUPLE, tab_.name, rid, ori_rec));
            }
        }
        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }
};