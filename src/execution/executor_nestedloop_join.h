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

const bool join_debug = false;

class NestedLoopJoinExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> left_;    // 左儿子节点（需要join的表）
    std::unique_ptr<AbstractExecutor> right_;   // 右儿子节点（需要join的表）
    size_t len_;                                // join后获得的每条记录的长度
    std::vector<ColMeta> cols_;                 // join后获得的记录的字段

    std::vector<Condition> fed_conds_;          // join条件
    bool isend;

   public:
    NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left, std::unique_ptr<AbstractExecutor> right, 
                            std::vector<Condition> conds) {
        left_ = std::move(left);
        right_ = std::move(right);
        len_ = left_->tupleLen() + right_->tupleLen();
        if (join_debug) {
            std::fstream outfile;
            outfile.open("output.txt",std::ios::out | std::ios::app);
            outfile << left_->tupleLen() << " " << right_->tupleLen() << "\n";
            outfile.close();
        }
        cols_ = left_->cols();
        auto right_cols = right_->cols();
        for (auto &col : right_cols) {
            col.offset += left_->tupleLen();
        }

        cols_.insert(cols_.end(), right_cols.begin(), right_cols.end());
        isend = false;
        fed_conds_ = std::move(conds);

    }

    ~NestedLoopJoinExecutor() override {}

    size_t tupleLen() const override { return len_; };

    const std::vector<ColMeta> &cols() const override { return cols_; }

    std::string getType() override { return "NestedLoopJoinExecutor"; };

    void beginTuple() override {
        if (join_debug) {
            std::fstream outfile;
            outfile.open("output.txt",std::ios::out | std::ios::app);
            outfile << "join_begin\n";
            outfile.close();
        }
        left_->beginTuple();
        right_->beginTuple();
        if (left_->is_end() || right_->is_end() || !satisfy(Next(), cols_, fed_conds_)) nextTuple();
    }

    void nextTuple() override {
        right_->nextTuple();
        for (; !left_->is_end(); left_->nextTuple(), right_->beginTuple()) {
            for (; !right_->is_end(); right_->nextTuple()) {
                if (satisfy(Next(), cols_, fed_conds_)) return; 
            }
        }
    }

    std::unique_ptr<RmRecord> Next() override {
        auto now = std::make_unique<RmRecord>(len_);
        memcpy(now->data, left_->Next().get()->data, left_->tupleLen());
        memcpy(now->data + left_->tupleLen(), right_->Next().get()->data, right_->tupleLen());
        return now;
    }

    bool is_end() const override { return left_->is_end(); }

    Rid &rid() override { return _abstract_rid; }
};