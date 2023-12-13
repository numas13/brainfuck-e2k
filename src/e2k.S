! SPDX-License-Identifier: GPL-3.0-only

#include "bc.h"

#define CODE %r0
#define TAPE %r1
#define TAPE_SIZE %r2
#define STATS %r3
#define PC %r4
#define CUR %r5
#define ACC %r6
#define STATS_OPS %r7
#define STATS_CALLS %r8
#define STATS_BRANCHES %r9
#define STATS_BRANCHES_TAKEN %r10
#define STATS_CPU_CYCLES %r11

! Pipeline registers
#define NPC_F %b[0]
#define NPC_D %b[2]
#define NPC_E %b[4]
#define INSN_F %b[1]
#define INSN_D %b[3]
#define INSN_E %b[5]
#define IMM_D %b[6]
#define IMM_E %b[8]
#define BRANCH_TARGET_D %b[7]
#define BRANCH_TARGET_E %b[9]

! Pipeline predictes
#define IS_NOT_ADD_D %pred0
#define IS_NOT_ADD_E %pred1
#define IS_NOT_MOV_D %pred2
#define IS_NOT_MOV_E %pred3
#define IS_NOT_EXE_D %pred4
#define IS_NOT_EXE_E %pred5
#define IS_NOT_CALL_D %pred6
#define IS_NOT_CALL_E %pred7
#define IS_NOT_BEQZ_D %pred8
#define IS_NOT_BEQZ_E %pred9
#define IS_NOT_BNEZ_D %pred10
#define IS_NOT_BNEZ_E %pred11

#define IS_NOT_BRANCH %pred27
#define IS_ACC_ZERO %pred28
#define IS_NOT_TAKEN %pred29
#define IS_BRANCH_NOT_TAKEN %pred30

#define LOOP %ctpr1
#define CALL %ctpr3

    .text
    .global run_program_e2k
    .type run_program_e2k, #function
    .balign 8
run_program_e2k:
    {
        setwd wsz=16, nfx=1, dbl=1
        setbn rsz=7, rbs=8, rcur=0
        setbp psz=13
        addd,1 TAPE, 0, CUR
        addd,2 0, 0, PC
        ldb,3 TAPE, 0, ACC
        disp %ctpr1, pipeline
    }
    {
        nop 3
        ldd,0 STATS, 0, STATS_OPS
        ldd,2 STATS, 8, STATS_CALLS
        ldd,3 STATS, 16, STATS_BRANCHES
        ldd,5 STATS, 24, STATS_BRANCHES_TAKEN
    }
    {
        rrd,0 %clkr, STATS_CPU_CYCLES
        ct %ctpr1
    }

call:
    {
        setwd wsz=12, nfx=1, dbl=1
        setbn rsz=3, rbs=8, rcur=0
        setbp psz=0
        cmpedb,0 IMM_E, FUNC_PUTC, %pred31
        cmpedb,1 IMM_E, FUNC_GETC, %pred30
        addd,2 STATS_CALLS, 1, STATS_CALLS
#ifdef FUNC_DEBUG
        cmpedb,3 IMM_E, FUNC_DEBUG, %pred29
#endif
        disp %ctpr1, putchar
    }
    {
        disp %ctpr2, getchar
    }
#ifdef FUNC_DEBUG
    {
        nop 2
        disp %ctpr3, debug
    }
#endif
    {
        addd,0 ACC, 0, %b[0] ? %pred31
        call %ctpr1, wbs=8 ? %pred31
    }
    {
        call %ctpr2, wbs=8 ? %pred30
    }
#ifdef FUNC_DEBUG
    {
        addd,0 CODE, 0, %b[0] ? %pred29
        subd,1 PC, 4, %b[1] ? %pred29
        addd,2 TAPE, 0, %b[2] ? %pred29
        addd,3 CUR, 0, %b[3] ? %pred29
        addd,4 ACC, 0, %b[4] ? %pred29
        call %ctpr3, wbs=8 ? %pred29
    }
#endif
    {
        setwd wsz=16, nfx=1, dbl=1
        setbn rsz=7, rbs=8, rcur=0
        setbp psz=13
        addd,0 %b[0], 0, ACC ? %pred30
    }

    ! Pipeline stages:
    ! II0 - F - Fetch (latency 3)
    ! II1 - D - Decode (latency 3)
    ! II2 - E - Execute (latency 3-7)
pipeline:
    {
        addd,0 PC, 4, NPC_E
        addd,1 PC, 8, NPC_D
        ldw,2,sm CODE, PC, INSN_E
        disp LOOP, loop
    }
    {
        nop 1
        ldw,2,sm CODE, NPC_E, INSN_D
        disp CALL, call
    }
    {
        cmpandesb,0,sm INSN_E, OP_MOV, IS_NOT_MOV_E
        cmpandesb,1,sm INSN_E, OP_BEQZ, IS_NOT_BEQZ_E
        getfd,2,sm INSN_E, 0x7686, IMM_E
    }
    {
        cmpandesb,0,sm INSN_E, OP_BNEZ, IS_NOT_BNEZ_E
        cmpandesb,1,sm INSN_E, OP_ADD, IS_NOT_ADD_E
        addd,2,sm NPC_E, IMM_E, BRANCH_TARGET_E
        cmpandesb,3,sm INSN_E, OP_EXE, IS_NOT_EXE_E
        cmpandesb,4,sm INSN_E, OP_CALL, IS_NOT_CALL_E
    }

loop:
    {
        ! Fetch
        addd,0 NPC_D, 4, NPC_F
        ldw,2,sm CODE, NPC_D, INSN_F
        ! Decode
        getfd,1,sm INSN_D, 0x7686, IMM_D
        ! Execute
        ldb,3 CUR, IMM_E, ACC ? ~IS_NOT_MOV_E
        cmpedb,4 ACC, 0, IS_ACC_ZERO
        stb,5 ACC, CUR, 0 ? ~IS_NOT_MOV_E
        pass IS_NOT_BEQZ_E, @p0
        pass IS_NOT_BNEZ_E, @p1
        landp @p0, @p1, @p4
        pass @p4, IS_NOT_BRANCH
    }
    {
        ! Decode
        addd,0,sm NPC_D, IMM_D, BRANCH_TARGET_D
        cmpandesb,1,sm INSN_D, OP_MOV, IS_NOT_MOV_D
        cmpandesb,3,sm INSN_D, OP_BEQZ, IS_NOT_BEQZ_D
        cmpandesb,4,sm INSN_D, OP_BNEZ, IS_NOT_BNEZ_D
        ! Execute
        addd,2 ACC, IMM_E, ACC ? ~IS_NOT_ADD_E
        addd,5 CUR, IMM_E, CUR ? ~IS_NOT_MOV_E
        pass IS_ACC_ZERO, @p0
        pass IS_NOT_BEQZ_E, @p1
        pass IS_NOT_BNEZ_E, @p2
        landp  @p0, ~@p1, @p4
        landp ~@p0, ~@p2, @p5
        landp ~@p4, ~@p5, @p6
        pass @p6, IS_NOT_TAKEN
    }
    {
        addd,0 STATS_OPS, 1, STATS_OPS

        ! Decode
        cmpandesb,1,sm INSN_D, OP_ADD, IS_NOT_ADD_D
        cmpandesb,3,sm INSN_D, OP_EXE, IS_NOT_EXE_D
        cmpandesb,4,sm INSN_D, OP_CALL, IS_NOT_CALL_D
        ! Execute
        pass IS_NOT_BRANCH, @p0
        pass IS_NOT_TAKEN, @p1
        landp ~@p0, @p1, @p4
        pass @p4, IS_BRANCH_NOT_TAKEN

        abn abnf=0, abnt=1
        abp abpf=0, abpt=1
        ct LOOP ? ~IS_NOT_EXE_E
    }
    {
        addd,3 NPC_E, 0, PC ? IS_NOT_TAKEN
        addd,4 BRANCH_TARGET_E, 0, PC ? ~IS_NOT_TAKEN

        ldw,0,sm CODE, NPC_E, INSN_E ? IS_NOT_TAKEN
        ldw,2,sm CODE, BRANCH_TARGET_E, INSN_E ? ~IS_NOT_TAKEN

        ct CALL ? ~IS_NOT_CALL_E
    }
    {
        addd,0 NPC_E, 4, NPC_E ? IS_NOT_TAKEN
        addd,1 BRANCH_TARGET_E, 4, NPC_E ? ~IS_NOT_TAKEN
        addd,3 PC, 8, NPC_D ? ~IS_NOT_TAKEN
    }
    {
        ldw,2,sm CODE, NPC_E, INSN_D ? ~IS_NOT_TAKEN

        addd,0 STATS_BRANCHES, 1, STATS_BRANCHES ? ~IS_NOT_BRANCH
        addd,1 STATS_BRANCHES_TAKEN, 1, STATS_BRANCHES_TAKEN ? ~IS_NOT_TAKEN

        abn abnf=0, abnt=1
        abp abpf=0, abpt=1
        ct LOOP ? IS_BRANCH_NOT_TAKEN
    }
    {
        cmpandesb,0,sm INSN_E, OP_MOV, IS_NOT_MOV_E
        cmpandesb,1,sm INSN_E, OP_BEQZ, IS_NOT_BEQZ_E
        getfd,2,sm INSN_E, 0x7686, IMM_E
    }
    {
        cmpandesb,0,sm INSN_E, OP_BNEZ, IS_NOT_BNEZ_E
        cmpandesb,1,sm INSN_E, OP_ADD, IS_NOT_ADD_E
        addd,2,sm NPC_E, IMM_E, BRANCH_TARGET_E
        cmpandesb,3,sm INSN_E, OP_EXE, IS_NOT_EXE_E
        cmpandesb,4,sm INSN_E, OP_CALL, IS_NOT_CALL_E

        ct LOOP ? ~IS_NOT_BRANCH
    }
    {
        ! remove end op from counter
        rrd,0 %clkr, %g16
        subd,1 STATS_OPS, 1, STATS_OPS
        return %ctpr3
    }
    {
        subd,0 %g16, STATS_CPU_CYCLES, STATS_CPU_CYCLES
        std,2 STATS_OPS, STATS, 0
        std,5 STATS_CALLS, STATS, 8
    }
    {
        std,2 STATS_BRANCHES, STATS, 16
        std,5 STATS_BRANCHES_TAKEN, STATS, 24
    }
    {
        nop 3
        std,2 STATS_CPU_CYCLES, STATS, 32
    }
        ct %ctpr3

    .size run_program_e2k, . - run_program_e2k
