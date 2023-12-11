// SPDX-License-Identifier: GPL-3.0-only

#ifndef BF_H
#define BF_H

#define OP_END          (0)
#define OP_MOV          (1 << 0)
#define OP_ADD          (1 << 1)
#define OP_BEQZ         (1 << 2)
#define OP_BNEZ         (1 << 3)
#define OP_CALL         (1 << 4)
#define OP_MASK         (0x3f)
#define OP_EXE          (OP_MOV | OP_ADD)
#define OP_BR           (OP_BEQZ | OP_BNEZ)

#define FUNC_PUTC       0
#define FUNC_GETC       1
#define FUNC_DEBUG      2

#endif // BF_H
