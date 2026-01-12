姓名：陈礼邦

学号：2023201805

因为实验都过的太久了，不是很想再复述一遍每个实验怎么完成，我就写几个让我印象比较深刻的点。

# lab1

这个实验是用来处理磁盘、缓冲区读写的，磁盘部分的很多系统函数我都是问chatgpt的，缓冲区部分就是一个lru的替换策略在ics里面也实现过，基本没有什么难度。

# lab2

这个实验就是写一棵B+树，上网学习一下B+树的行为再写就行，调试比较麻烦。

这个实验有两个不太好的地方。

第一个是在ix_compare里面没有四字节对齐仍然用指针强行转化成int或float，这样是有很大风险的，应该使用memcpy函数进行赋值。

```cpp
inline int ix_compare(const char *a, const char *b, ColType type, int col_len) {
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
```
这种写法才是严谨的。

第二个是并发测试里面google test写的不对，我验收和助教说了但是助教不以为然。并发测试应该是每个线程分别把不同的数插入到B+树里面，但是插入方法是让每个线程都把全部插入一遍，这就相当于没有并行。

# lab3

这个实验就是写算子，感觉这是最简单的一个实验，因为要写的代码不超过50行。其中排序算子和很多的优化方法都没有涉及到。

我觉得应该文档里面多给一点，让我们实现一些比较常见的优化，而不是像这样用暴力写两个for循环就没了。

我在调试的时候发现网络端口写的有bug，如果出错再运行会bind error

根据chatgpt提供的思路，这是因为进程没有杀干净，需要手动杀一下进程：

```
clb@clb-ThinkBook-16-G5-IRH:~/Desktop/lab/rucbase-lab/src/test/query$ ss -lntp | grep 8765
LISTEN 0      8            0.0.0.0:8765       0.0.0.0:*    users:(("rmdb",pid=15234,fd=3))
clb@clb-ThinkBook-16-G5-IRH:~/Desktop/lab/rucbase-lab/src/test/query$ kill -9 15234
```

# lab4

我没有实现间隙锁，而是写了一个表锁（就是对表或者tuple加上读，写，意向读，意向写锁）。这样实现会容易一些（主要是我只知道这种方法）。

```cpp
class LockManager {
    /* 加锁类型，包括共享锁、排他锁、意向共享锁、意向排他锁、SIX（意向排他锁+共享锁） */
    enum class LockMode { SHARED, EXLUCSIVE, INTENTION_SHARED, INTENTION_EXCLUSIVE, S_IX };

    /* 用于标识加锁队列中排他性最强的锁类型，例如加锁队列中有SHARED和EXLUSIVE两个加锁操作，则该队列的锁模式为X */
    // enum class GroupLockMode { NON_LOCK, IS, IX, S, X, SIX};

    /* 事务的加锁申请 */
    // class LockRequest {
    // public:
    //     LockRequest(txn_id_t txn_id, LockMode lock_mode)
    //         : txn_id_(txn_id), lock_mode_(lock_mode), granted_(false) {}

    //     txn_id_t txn_id_;   // 申请加锁的事务ID
    //     LockMode lock_mode_;    // 事务申请加锁的类型
    //     bool granted_;          // 该事务是否已经被赋予锁
    // };

    /* 数据项上的加锁队列 */
    // class LockRequestQueue {
    // public:
    //     std::list<LockRequest> request_queue_;  // 加锁队列
    //     std::condition_variable cv_;            // 条件变量，用于唤醒正在等待加锁的申请，在no-wait策略下无需使用
    //     GroupLockMode group_lock_mode_ = GroupLockMode::NON_LOCK;   // 加锁队列的锁模式
    // };

public:
    LockManager() {}

    ~LockManager() {}

    bool lock_shared_on_record(Transaction* txn, const Rid& rid, int tab_fd);

    bool lock_exclusive_on_record(Transaction* txn, const Rid& rid, int tab_fd);

    bool lock_shared_on_table(Transaction* txn, int tab_fd);

    bool lock_exclusive_on_table(Transaction* txn, int tab_fd);

    bool lock_IS_on_table(Transaction* txn, int tab_fd);

    bool lock_IX_on_table(Transaction* txn, int tab_fd);

    // bool unlock(Transaction* txn, LockDataId lock_data_id);
    bool unlock(Transaction* txn);

    bool work(Transaction *txn, LockDataId id, int type);

private:
    std::mutex latch_;      // 用于锁表的并发
    // std::unordered_map<LockDataId, LockRequestQueue> lock_table_;   // 全局锁表

    std::unordered_map<LockDataId, std::vector<int>> lock_num;
};
```

我用lock_num来记录每个锁上了多少次，然后用某种锁的总数减去事务已经持有该种锁的个数，就能判断其他事务有没有持有这一种锁。

logmanager貌似没有任何用，测试不需要用到。

测试部分的问题是幻读测试部分调用的是seqscan而不是indexscan，但是又用diff来比较，应该改称dict才对。

# 总结

我觉得这个lab的难度不是很够，lab1,lab2没有太大问题，lab3给的指引不够（写暴力太容易），lab4的测试数据太弱了。

没有自己写编译器的部分，应该要手写一个编译器或者用yacc之类的弄一次。

没有断电恢复内容，应该要把这一个部分的内容给加上。