﻿
#include "mcore/mcore.h"
#include "elfloadimage.hh"

typedef struct funcdata     funcdata;
typedef struct pcodeop      pcodeop;
typedef struct varnode      varnode;
typedef struct flowblock    flowblock;
typedef struct dobc         dobc;

class pcodeemit2 : public PcodeEmit {
public:
    funcdata *fd;
    virtual void dump(const Address &address, OpCode opc, VarnodeData *outvar, VarnodeData *vars, int size);
};

struct varnode {
    struct {
        unsigned    mark : 1;
        unsigned    constant : 1;
        unsigned    annotation : 1;
        unsigned    input : 1;          // 没有祖先
        unsigned    writtern : 1;       // 是def
        unsigned    insert : 1;
        unsigned    implied : 1;        // 是一个临时变量
        unsigned    exlicit : 1;        // 不是临时变量

        unsigned    covertdirty : 1;    // cover没跟新
    } flags = { 0 };

    int size = 0;
    int create_index = 0;
    Address loc;

    pcodeop     *def = NULL;
    uintb       nzm;

    varnode(int s, const Address &m);
    ~varnode();

    const Address &get_addr() { return (const Address &)loc; }
};

class pcodeblk {
    struct dynarray op;
};

struct pcodeop {
    struct {
        unsigned startbasic : 1;
        unsigned branch : 1;
        unsigned call : 1;
        unsigned returns : 1;
        unsigned nocollapse : 1;
        unsigned dead : 1;
        unsigned marker : 1;        // 特殊的站位符， (phi 符号 或者 间接引用 或 CPUI_COPY 对同一个变量的操作)，
        unsigned boolouput : 1;     // 布尔操作

        unsigned coderef : 1;
        unsigned startmark : 1;     // instruction的第一个pcode
    } flags;

    OpCode opcode;
    /* 一个指令对应于多个pcode，这个是用来表示inst的 */
    SeqNum start;               
    flowblock *parent;

    varnode *output;
    vector<varnode *> inrefs;

    list<pcodeop *>::iterator basiciter;
    list<pcodeop *>::iterator insertiter;
    list<pcodeop *>::iterator codeiter;

    pcodeop(int s, const SeqNum &sq);
    ~pcodeop();

    void            set_opcode(OpCode op);
    varnode*        get_in(int slot) { return inrefs[slot];  }
    const Address&  get_addr() { return start.getAddr();  }
};

struct flowblock {
    enum {
        t_condition,
        t_if,
        t_whiledo,
        t_dowhile,
    } type;

    struct dynarray     ops;

    flowblock *parent;
    flowblock *immed_dom;

    int index;
    int numdesc;        // 在 spaning tree中的后代数量

    struct dynarray     in_edges;
    struct dynarray     out_edges;

    flowblock();
    ~flowblock();
};

typedef map<SeqNum, pcodeop *>  pcodeop_tree;
typedef struct op_edge     op_edge;

struct op_edge {
    pcodeop *from;
    pcodeop *to;

    op_edge(pcodeop *from, pcodeop *to);
    ~op_edge();
} ;

struct funcdata {
    struct VisitStat {
        SeqNum seqnum;
        int size;
    };

    pcodeop_tree     optree;
    AddrSpace   *uniq_space = NULL;

    struct {
        funcdata *next = NULL;
        funcdata *prev = NULL;
    } node;

    list<op_edge *>    edgelist;

    /* jmp table */
    vector<pcodeop *>   tablelist;

    list<pcodeop *>     deadlist;
    list<pcodeop *>     alivelist;
    list<pcodeop *>     storelist;
    list<pcodeop *>     loadlist;
    list<pcodeop *>     useroplist;
    list<pcodeop *>     deadandgone;
    int op_uniqid = 0;

    map<Address,VisitStat> visited;
    dobc *d = NULL;

    struct {
        long uniqbase = 0;
        int uniqid = 0;
        int create_index = 0;
        struct dynarray all = { 0 };
    } vbank;

    /* control-flow graph */
    struct {
        vector<flowblock *>     list;
    } cfg;

    Address addr;
    Address eaddr;
    char *name;
    int size = 0;

    /* 扫描到的最小和最大指令地址 */
    Address minaddr;
    Address maxaddr;
    int inst_count = 0;
    int inst_max = 1000000;

    vector<Address>     addrlist;
    pcodeemit2 emitter;

    funcdata(const char *name, const Address &a, int size, dobc *d);
    ~funcdata(void);


    pcodeop*    newop(int inputs, SeqNum &sq);
    pcodeop*    newop(int inputs, const Address &pc);

    varnode*    new_varnode_out(int s, const Address &m, pcodeop *op);
    varnode*    new_varnode(int s, AddrSpace *base, uintb off);
    varnode*    new_varnode(int s, const Address &m);

    varnode*    create_vn(int s, const Address &m);
    varnode*    create_def(int s, const Address &m, pcodeop *op);
    varnode*    create_def_unique(int s, pcodeop *op);

    void        op_set_opcode(pcodeop *op, OpCode opc);
    void        op_set_input(pcodeop *op, varnode *vn, int slot);
    pcodeop*    find_op(const SeqNum &num) const;
    void        del_op(pcodeop *op);
    void        del_varnode(varnode *vn);

    void        del_remaining_ops(list<pcodeop *>::const_iterator oiter);
    void        new_address(pcodeop *from, const Address &to);
    pcodeop*    find_rel_target(pcodeop *op, Address &res) const;
    pcodeop*    target(const Address &addr) const;
    pcodeop*    branch_target(pcodeop *op);

    bool        set_fallthru_bound(Address &bound);
    void        fallthru();
    pcodeop*    xref_control_flow(list<pcodeop *>::const_iterator oiter, bool &startbasic, bool &isfallthru);
    void        generate_ops();
    bool        process_instruction(const Address &curaddr, bool &startbasic);
    void        analysis_jmptable(pcodeop *op);

    void        add_op_edge(pcodeop *from, pcodeop *to);
    void        collect_edges();
    void        generate_blocks();

    void        dump_inst();

    void        remove_from_codelist(pcodeop *op);
    void        op_insert_before(pcodeop *op, pcodeop *follow);
    void        op_insert_after(pcodeop *op, pcodeop *prev);
};

struct dobc {
    ElfLoadImage *loader;
    string filename;
    string slafilename;

    ContextDatabase *context = NULL;
    Translate *trans = NULL;
    TypeFactory *types;

    struct {
        int counts = 0;
        funcdata *list = NULL;
    } funcs;

    int max_basetype_size;
    int min_funcsymbol_size;
    int max_instructions;

    vector<TypeOp *> inst;

    dobc(const char *slafilename, const char *filename);
    ~dobc();

    int init();

    /* 在一个函数内inline另外一个函数 */
    int inline_func(LoadImageFunc &func1, LoadImageFunc &func2);
    int loop_unrolling(LoadImageFunc &func1, Address &pos);
    /* 设置安全区，安全区内的代码是可以做别名分析的 */
    int set_safe_room(Address &pos, int size);

    void analysis();
    void run();
    void dump_function(LoadImageFunc &func1);
    AddrSpace *get_code_space() { return trans->getDefaultCodeSpace();  }

    void plugin_dvmp360();
    void plugin_dvmp();
};