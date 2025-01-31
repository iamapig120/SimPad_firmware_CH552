/**
 * NyaSama Core Virtualize Module
 * NSDN (C) 2014 - 2020
 * Version: 0.01a
 **/
#include "cvm.h"

#include <string.h>

#include "instr.h"

/* ---------------- 指令注册开始 ---------------- */
/*
 * 指令注册方式: { 函数指针, 指令类型 }
 * 其中，指令类型有：
 *      CVM_OP_NARG     无操作数，长度为1字节
 *      CVM_OP_DST8     含一个1字节的操作数，长度为2字节
 *      CVM_OP_DST      含一个1字节的操作数类型和一个2字节的操作数，长度为4字节
 *      CVM_OP_DST_SRC  含有两个1字节的操作数类型和两个2字节的操作数，并附加一个保留字节，长度为8字节
 * 
 * 指令实现见 instr.h
 */
static __code CVM_FUNC cvmFuncList[] = {
    { &__instr_nop,     CVM_OP_NARG },
    { &__instr_jmp,     CVM_OP_DST8 },
    { &__instr_clr,     CVM_OP_NARG },
    { &__instr_prt,     CVM_OP_DST8 },
    { &__instr_hidp,    CVM_OP_NARG },
    { &__instr_strp,    CVM_OP_DST8 },
    { &__instr_out,     CVM_OP_DST },
    { &__instr_keyp,    CVM_OP_NARG },
    { &__instr_ldi,     CVM_OP_DST },
    { &__instr_lde,     CVM_OP_DST },
    { &__instr_wri,     CVM_OP_DST },
    { &__instr_wre,     CVM_OP_DST },
    { &__instr_sleep,   CVM_OP_DST },
    { &__instr_led,     CVM_OP_DST_SRC },
    { &__instr_time,    CVM_OP_DST },
    { &__instr_fade,    CVM_OP_DST_SRC },
    { &__instr_trig,    CVM_OP_DST },
    { &__instr_rgb,     CVM_OP_DST },
    { &__instr_sysrst,  CVM_OP_DST },
    { &__instr_reload,  CVM_OP_DST },
    { &__instr_iap,     CVM_OP_DST }
};
/* ---------------- 指令注册结束 ---------------- */

static __idata bool cvmEndFlag = false;             // 解释器结束标志
static __idata cvm_ret (*cvmWDTCallback)() = NULL;  // 解释器看门狗回调
static __idata cvm_addr cvmProgCnt = 0;             // 程序计数器
#define CVM_JMP_DUMMY  0xFFFF
static __idata cvm_addr cvmJmpAddr = CVM_JMP_DUMMY; // 跳转寄存器

static __idata CVM_ERR_INFO cvmErrInfo;             // 错误指示器

/*
 * 获取错误指示器的指针，用于获取详细的错误信息
 */
CVM_ERR_INFO* cvm_err_info() {
    return &cvmErrInfo;
}

/*
 * 终止解释器
 */
void cvm_end() {
    cvmEndFlag = true;
}

/*
 * 将跳转寄存器设置为某个地址，解释器将在下个周期跳转
 */
void cvm_jmp(cvm_addr addr) {
    cvmJmpAddr = addr;
}

/*
 * 设置解释器看门狗回调
 */
void cvm_wdt(cvm_ret (*callback)()) {
    cvmWDTCallback = callback;
}

/*
 * 执行单条指令
 */
cvm_ret cvm_exe(CVM_OP* op) {
#if CVM_FUNC_MAX <= 0xFF
    return cvmFuncList[op->op_index].func(op);
#elif defined(CVM_LONGLEN_OP)
    #if CVM_FUNC_MAX == 0x1FF
        return cvmFuncList[op->op_index | ((op->type_dst_expr & 0x80) << 1)].func(op);
    #elif CVM_FUNC_MAX == 0x3FF
        return cvmFuncList[op->op_index | ((op->type_dst_expr & 0x80) << 1) | ((op->type_src & 0x80) << 2)].func(op);
    #elif CVM_FUNC_MAX == 0x7FF
        return cvmFuncList[op->op_index | ((op->type_dst_expr & 0x80) << 1) | ((op->type_src & 0x80) << 2) | ((op->type_ext & 0x80) << 3)].func(op);
    #endif
#endif
}

/*
 * 获取下一条指令长度
 */
uint8_t __cvm_get_op_len(uint8_t* code) {
#ifdef CVM_VARLEN_OP
    #if CVM_FUNC_MAX <= 0xFF
        return cvmFuncList[*code].length;
    #elif defined(CVM_LONGLEN_OP)
        #if CVM_FUNC_MAX == 0x1FF
            return cvmFuncList[*code | ((*(code + 1) & 0x80) << 1)].length;
        #elif CVM_FUNC_MAX == 0x3FF
            return cvmFuncList[*code | ((*(code + 1) & 0x80) << 1) | ((*(code + 6) & 0x80) << 2)].length;
        #elif CVM_FUNC_MAX == 0x7FF
            return cvmFuncList[*code | ((*(code + 1) & 0x80) << 1) | ((*(code + 6) & 0x80) << 2) | ((*(code + 11) & 0x80) << 3)].length;
        #endif
    #endif
#else
    return sizeof(CVM_OP);
#endif
}

#define ___CVM_GET_U32(addr) ((uint32_t) (*(addr) | (*(addr + 1) << 8) | (*(addr + 2) << 16) | (*(addr + 3) << 24)))
#define ___CVM_GET_U16(addr) ((uint16_t) (*(addr) | (*(addr + 1) << 8)))

/*
 * 从二进制码中获取指令结构
 * 其中 length 通过 __cvm_get_op_len 得到，op 为输出参数
 */
void __cvm_parse_op(uint8_t* code, uint8_t length, CVM_OP* op) {
    memset(op, 0, sizeof(CVM_OP));

    #ifdef CVM_VARLEN_OP
        if (length >= CVM_OP_NARG) {
            op->op_index = *code;
        #ifdef CVM_LONGLEN_OP
            if (length >= CVM_OP_DST8) {
                op->type_dst_expr = *(code + 1);
                if (length >= CVM_OP_DST) {
                    op->dst= ___CVM_GET_U32(code + 2);
                    if (length >= CVM_OP_DST_SRC) {
                        op->type_src = *(code + 6);
                        op->src= ___CVM_GET_U32(code + 7);
                        if (length == CVM_OP_DST_SRC_EXT) {
                            op->type_ext = *(code + 11);
                            op->ext= ___CVM_GET_U32(code + 12);
                        }
                    }
                }
            }
        #else
            if (length >= CVM_OP_DST8) {
                op->type_dst_expr = *(code + 1);
                if (length >= CVM_OP_DST) {
                    op->dst= ___CVM_GET_U16(code + 2);
                    if (length == CVM_OP_DST_SRC) {
                        op->type_src = *(code + 4);
                        op->src= ___CVM_GET_U16(code + 5);
                    }
                }
            }
        #endif
        }
    #else
        if (length == sizeof(CVM_OP)) {
            op->op_index = *code;
        #ifdef CVM_LONGLEN_OP
            op->type_dst_expr = *(code + 1);
            op->dst= *(code + 2) | (*(code + 3) << 8) | (*(code + 4) << 16) | (*(code + 5) << 24);
            op->type_src = *(code + 6);
            op->src= *(code + 7) | (*(code + 8) << 8) | (*(code + 9) << 16) | (*(code + 10) << 24);
            op->type_ext = *(code + 11);
            op->ext= *(code + 12) | (*(code + 13) << 8) | (*(code + 14) << 16) | (*(code + 15) << 24);
        #else
            op->type_dst_expr = *(code + 1);
            op->dst= *(code + 2) | (*(code + 3) << 8);
            op->type_src = *(code + 4);
            op->src= *(code + 5) | (*(code + 6) << 8);
            op->reversed = 0xFF;
        #endif
        }
    #endif
}

/*
 * 执行二进制程序
 * 其中 length 为二进制程序长度
 */
cvm_ret cvm_run(uint8_t* code, cvm_addr length) {
    uint8_t op_length = 0; uint8_t* offset = code;
    CVM_OP op_body;
    cvm_ret result = CVM_RET_OK;
    cvmEndFlag = false;
    cvmProgCnt = 0;

    memset(&cvmErrInfo, 0, sizeof(CVM_ERR_INFO));

    while (cvmProgCnt < length) {
        if (cvmWDTCallback != NULL) {
            result = cvmWDTCallback();
            if (result != CVM_RET_OK)
                return CVM_RET_ERR;
        }

        if (cvmEndFlag == true) {
            cvmEndFlag = false;
            break;
        }

        offset = code + cvmProgCnt;

        op_length = __cvm_get_op_len(offset);
        if (cvmProgCnt + op_length > length) {
            cvmErrInfo.msg = 'L';
            cvmErrInfo.addr = cvmProgCnt;
            return CVM_RET_ERR;
        }

        __cvm_parse_op(offset, op_length, &op_body);

        result = cvm_exe(&op_body);

        if (result == CVM_RET_ERR) {
            cvmErrInfo.msg = 'E';
            cvmErrInfo.addr = cvmProgCnt;
            memcpy(&(cvmErrInfo.op), &op_body, sizeof(CVM_OP));
            return CVM_RET_ERR;
        }

        if (cvmProgCnt >= length) {
            cvmErrInfo.msg = 'A';
            cvmErrInfo.addr = cvmProgCnt;
            return CVM_RET_ERR;
        }

        if (cvmJmpAddr != CVM_JMP_DUMMY) {
            if (cvmJmpAddr >= length) {
                cvmErrInfo.msg = 'J';
                cvmErrInfo.addr = cvmProgCnt;
                memcpy(&(cvmErrInfo.op), &op_body, sizeof(CVM_OP));
                return CVM_RET_ERR;
            }
            cvmProgCnt = cvmJmpAddr;
            cvmJmpAddr = CVM_JMP_DUMMY;
        } else {
            if (result == CVM_RET_END)
                break;
            else if (result == CVM_RET_RST)
                cvmProgCnt = 0;
            else
                cvmProgCnt += op_length;
        }
    }

    return CVM_RET_OK;
}
