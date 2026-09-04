// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <shader/usse_decoder_helpers.h>
#include <shader/usse_disasm.h>
#include <shader/usse_translator.h>
#include <shader/usse_types.h>
#include <util/log.h>

using namespace shader;
using namespace usse;

static Opcode decode_test_inst(const Imm2 alu_sel, const Imm4 alu_op, const Imm1 prec, DataType &filled_dt) {
    static const Opcode vf16_opcode[16] = {
        Opcode::INVALID,
        Opcode::INVALID,
        Opcode::VF16ADD,
        Opcode::VF16FRC,
        Opcode::VRCP,
        Opcode::VRSQ,
        Opcode::VLOG,
        Opcode::VEXP,
        Opcode::VF16DP,
        Opcode::VF16MIN,
        Opcode::VF16MAX,
        Opcode::VF16DSX,
        Opcode::VF16DSY,
        Opcode::VF16MUL,
        Opcode::VF16SUB,
        Opcode::INVALID,
    };

    static const Opcode vf32_opcode[16] = {
        Opcode::INVALID,
        Opcode::INVALID,
        Opcode::VADD,
        Opcode::VFRC,
        Opcode::VRCP,
        Opcode::VRSQ,
        Opcode::VLOG,
        Opcode::VEXP,
        Opcode::VDP,
        Opcode::VMIN,
        Opcode::VMAX,
        Opcode::VDSX,
        Opcode::VDSY,
        Opcode::VMUL,
        Opcode::VSUB,
        Opcode::INVALID,
    };

    static const Opcode test_ops[4][16] = {
        {
            Opcode::FADD,
            Opcode::INVALID,
            Opcode::INVALID,
            Opcode::FSUBFLR,
            Opcode::FRCP,
            Opcode::FRSQ,
            Opcode::FLOG,
            Opcode::FEXP,
            Opcode::FDP,
            Opcode::FMIN,
            Opcode::FMAX,
            Opcode::FDSX,
            Opcode::FDSY,
            Opcode::FMUL,
            Opcode::FSUB,
            Opcode::INVALID,
        },
        { Opcode::INVALID,
            Opcode::INVALID,
            Opcode::INVALID,
            Opcode::INVALID,
            Opcode::INVALID,
            Opcode::INVALID,
            Opcode::IADD16,
            Opcode::ISUB16,
            Opcode::IMUL16,
            Opcode::IADDU16,
            Opcode::ISUBU16,
            Opcode::IMULU16,
            Opcode::IADD32,
            Opcode::IADDU32,
            Opcode::ISUB32,
            Opcode::ISUBU32 },
        { Opcode::IADD8,
            Opcode::ISUB8,
            Opcode::IADDU8,
            Opcode::ISUBU8,
            Opcode::IMUL8,
            Opcode::FPMUL8,
            Opcode::IMULU8,
            Opcode::FPADD8,
            Opcode::FPSUB8,
            Opcode::INVALID,
            Opcode::INVALID,
            Opcode::INVALID,
            Opcode::INVALID,
            Opcode::INVALID,
            Opcode::INVALID,
            Opcode::INVALID },
        { Opcode::AND,
            Opcode::OR,
            Opcode::XOR,
            Opcode::SHL,
            Opcode::SHR,
            Opcode::ROL,
            Opcode::INVALID,
            Opcode::ASR,
            Opcode::INVALID,
            Opcode::INVALID,
            Opcode::INVALID,
            Opcode::INVALID,
            Opcode::INVALID,
            Opcode::INVALID,
            Opcode::INVALID,
            Opcode::INVALID }
    };

    if (alu_sel == 0) {
        switch (prec) {
        case 0: {
            filled_dt = DataType::F16;
            return vf16_opcode[alu_op];
        }

        case 1: {
            filled_dt = DataType::F32;
            return vf32_opcode[alu_op];
        }

        default: return Opcode::INVALID;
        }
    }

    const auto test_op = test_ops[alu_sel][alu_op];

    switch (test_op) {
    case Opcode::IADDU32:
    case Opcode::ISUBU32: {
        filled_dt = DataType::UINT32;
        break;
    }
    case Opcode::ISUB32:
    case Opcode::IADD32: {
        filled_dt = DataType::INT32;
        break;
    }
    case Opcode::IADD16:
    case Opcode::ISUB16:
    case Opcode::IMUL16: {
        filled_dt = DataType::INT16;
        break;
    }
    case Opcode::IADDU16:
    case Opcode::ISUBU16:
    case Opcode::IMULU16: {
        filled_dt = DataType::UINT16;
        break;
    }
    case Opcode::IADD8:
    case Opcode::ISUB8:
    case Opcode::IMUL8: {
        filled_dt = DataType::INT8;
        break;
    }
    case Opcode::IADDU8:
    case Opcode::ISUBU8:
    case Opcode::FPSUB8:
    case Opcode::IMULU8: {
        filled_dt = DataType::UINT8;
        break;
    }
    default: {
        break;
    }
    }

    if (alu_sel == 2) {
        filled_dt = DataType::UINT8;
    }

    if (alu_sel == 3) {
        filled_dt = DataType::UINT32;
    }

    return test_op;
}

inline static bool is_sub_opcode(Opcode test_op) {
    return (test_op == Opcode::VSUB) || (test_op == Opcode::VF16SUB) || (test_op == Opcode::ISUB8) || (test_op == Opcode::ISUB16) || (test_op == Opcode::ISUB32) || (test_op == Opcode::ISUBU8) || (test_op == Opcode::ISUBU16) || (test_op == Opcode::ISUBU32) || (test_op == Opcode::FPSUB8);
}

spv::Id USSETranslatorVisitor::vtst_impl(Instruction inst, ExtPredicate pred, int zero_test, int sign_test, Imm4 load_mask, bool mask) {
    // Usually we would expect this to have a compare behavior
    // Comparison is done by subtracting the first src by the second src, and compare the result value.
    // We currently optimize for that case first
    const DataType load_data_type = inst.opr.src1.type;
    const DataType store_data_type = inst.opr.dest.type;

    bool has_4_comp = std::popcount(load_mask) == 4;

    static const spv::Op tb_comp_ops[3][2][4] = {
        { { spv::OpFOrdNotEqual,
              spv::OpFOrdLessThan,
              spv::OpFOrdGreaterThan,
              spv::OpAll },
            { spv::OpFOrdEqual,
                spv::OpFOrdLessThanEqual,
                spv::OpFOrdGreaterThanEqual,
                spv::OpAll } },
        { { spv::OpINotEqual,
              spv::OpSLessThan,
              spv::OpSGreaterThan,
              spv::OpAll },
            { spv::OpIEqual,
                spv::OpSLessThanEqual,
                spv::OpSGreaterThanEqual,
                spv::OpAll } },
        { { spv::OpINotEqual,
              spv::OpULessThan,
              spv::OpUGreaterThan,
              spv::OpAll },
            { spv::OpIEqual,
                spv::OpULessThanEqual,
                spv::OpUGreaterThanEqual,
                spv::OpAll } },
    };

    // Load our compares
    spv::Id lhs = spv::NoResult;
    spv::Id rhs = spv::NoResult;

    const spv::Id pred_type = utils::make_vector_or_scalar_type(m_b, m_b.makeBoolType(), has_4_comp ? 4 : 1);

    // Zero test number:
    // 0 - alway pass
    // 1 - zero
    // 2 - non-zero
    const bool compare_include_equal = (zero_test == 1);

    // Sign test number

    int index_tb_comp = 0;
    if (is_signed_integer_data_type(load_data_type)) {
        index_tb_comp = 1;
    } else if (is_unsigned_integer_data_type(load_data_type)) {
        index_tb_comp = 2;
    }

    spv::Op used_comp_op = tb_comp_ops[index_tb_comp][compare_include_equal][sign_test];
    bool signed_zero_test = false;

    // Optimize this case. Alternative name is CMP.
    const char *tb_comp_str[2][4] = {
        { "ne",
            "lt",
            "gt",
            "inv" },
        { "equal",
            "le",
            "ge",
            "inv" }
    };

    const char *used_comp_str = tb_comp_str[compare_include_equal][sign_test];

    m_b.setDebugSourceLocation(m_recompiler.cur_pc, nullptr);

    if (is_sub_opcode(inst.opcode)) {
        if (mask) {
            LOG_DISASM("{:016x}: {}{}.{}.{}.{} {} {} {}", m_instr, disasm::e_predicate_str(pred), "CMPMSK", used_comp_str, disasm::data_type_str(store_data_type),
                disasm::data_type_str(load_data_type), disasm::operand_to_str(inst.opr.dest, load_mask), disasm::operand_to_str(inst.opr.src1, load_mask),
                disasm::operand_to_str(inst.opr.src2, load_mask));
        } else {
            LOG_DISASM("{:016x}: {}{}.{}.{} p{} {} {}", m_instr, disasm::e_predicate_str(pred), "CMP", used_comp_str, disasm::data_type_str(load_data_type),
                inst.opr.dest.num, disasm::operand_to_str(inst.opr.src1, load_mask), disasm::operand_to_str(inst.opr.src2, load_mask));
        }

        lhs = load(inst.opr.src1, load_mask);
        rhs = load(inst.opr.src2, load_mask);
    } else {
        if (mask) {
            LOG_DISASM("{:016x}: {}{}.{}zero.{}.{} {} {} {}", m_instr, disasm::e_predicate_str(pred), disasm::opcode_str(inst.opcode), used_comp_str, disasm::data_type_str(store_data_type),
                disasm::data_type_str(load_data_type), disasm::operand_to_str(inst.opr.dest, load_mask), disasm::operand_to_str(inst.opr.src1, load_mask), disasm::operand_to_str(inst.opr.src2, load_mask));
        } else {
            LOG_DISASM("{:016x}: {}{}.{}zero.{} p{} {} {}", m_instr, disasm::e_predicate_str(pred), disasm::opcode_str(inst.opcode), used_comp_str, disasm::data_type_str(load_data_type),
                inst.opr.dest.num, disasm::operand_to_str(inst.opr.src1, load_mask), disasm::operand_to_str(inst.opr.src2, load_mask));
        }

        lhs = do_alu_op(inst, load_mask, has_4_comp ? 0b1111 : 0b1);

        const spv::Id c0_type = utils::make_vector_or_scalar_type(m_b, m_b.makeFloatType(32), has_4_comp ? 4 : 1);
        spv::Id c0 = utils::make_uniform_vector_from_type(m_b, c0_type, 0.0f);

        if (is_signed_integer_data_type(load_data_type)) {
            c0 = m_b.makeIntConstant(0);
        } else if (load_data_type == DataType::UINT32 && (sign_test == 1 || sign_test == 2)) {
            // The sign tests on a 32-bit bitwise/unsigned result look at bit 31 of the result
            // (the compiler tests a flag with "SHL.ltzero.u32 p0 reg 31"); an unsigned
            // less-than-zero would never be true.
            const spv::Id int_type = utils::make_vector_or_scalar_type(m_b, m_b.makeIntType(32), has_4_comp ? 4 : 1);
            lhs = m_b.createUnaryOp(spv::OpBitcast, int_type, lhs);
            c0 = m_b.makeIntConstant(0);
            signed_zero_test = true;
        } else if (is_unsigned_integer_data_type(load_data_type)) {
            c0 = m_b.makeUintConstant(0);
        }

        rhs = c0;
    }

    if (lhs == spv::NoResult || rhs == spv::NoResult) {
        LOG_ERROR("Source not loaded (lhs: {}, rhs: {})", lhs, rhs);
        return spv::NoResult;
    }

    if (signed_zero_test) {
        used_comp_op = tb_comp_ops[1][compare_include_equal][sign_test];
    }
    if (!is_sub_opcode(inst.opcode) && sign_test == 2 && !compare_include_equal) {
        // Sony's listing (psp2shaderperf -disasm) of a psp2cgc program: "shl.poszero.u32 p0,
        // pa0.x, 0x1f" (sign test 1) is the flag-set test and "shl.negzero.u32" (sign test 2)
        // the flag-clear test on the same register, so sign test 2 is "sign bit clear", zero
        // included: >= 0, not > 0.
        used_comp_op = tb_comp_ops[signed_zero_test ? 1 : index_tb_comp][1][sign_test];
    }

    return m_b.createOp(used_comp_op, pred_type, { lhs, rhs });
}

bool USSETranslatorVisitor::vtst(
    ExtPredicate pred,
    Imm1 skipinv,
    Imm1 onceonly,
    Imm1 syncstart,
    Imm1 dest_ext,
    Imm1 src1_neg,
    Imm1 src1_ext,
    Imm1 src2_ext,
    Imm1 prec,
    Imm1 src2_vscomp,
    RepeatCount rpt_count,
    Imm2 sign_test,
    Imm2 zero_test,
    Imm1 test_crcomb_and,
    Imm3 chan_cc,
    Imm2 pdst_n,
    Imm2 dest_bank,
    Imm2 src1_bank,
    Imm2 src2_bank,
    Imm7 dest_n,
    Imm1 test_wben,
    Imm2 alu_sel,
    Imm4 alu_op,
    Imm7 src1_n,
    Imm7 src2_n) {
    DataType load_data_type;
    Opcode test_op = decode_test_inst(alu_sel, alu_op, prec, load_data_type);

    if (test_op == Opcode::INVALID) {
        LOG_ERROR("Unsupported comparision: {} {}", alu_sel, alu_op);
        return false;
    }

    Instruction inst;
    inst.opcode = test_op;

    const Imm4 tb_decode_load_mask[] = {
        0b0001,
        0b0010,
        0b0100,
        0b1000,
        0b0000,
        0b0000,
        0b0000
    };

    if (chan_cc >= 4) {
        LOG_ERROR("Unsupported comparision with requested channels ordinal: {}", chan_cc);
        return false;
    }

    const Imm4 load_mask = tb_decode_load_mask[chan_cc];

    const bool use_double_reg = alu_sel == 0;
    const uint8_t bits_max = use_double_reg ? 8 : 7;

    // Build up source
    inst.opr.src1 = decode_src12(inst.opr.src1, src1_n, src1_bank, src1_ext, use_double_reg, bits_max, m_second_program);
    inst.opr.src2 = decode_src12(inst.opr.src2, src2_n, src2_bank, src2_ext, use_double_reg, bits_max, m_second_program);

    inst.opr.src1.type = load_data_type;
    inst.opr.src2.type = load_data_type;

    inst.opr.src1.swizzle = SWIZZLE_CHANNEL_4_DEFAULT;
    inst.opr.src2.swizzle = (src2_vscomp && (alu_sel == 0)) ? (Swizzle4 SWIZZLE_CHANNEL_4(X, X, X, X)) : (Swizzle4 SWIZZLE_CHANNEL_4_DEFAULT);

    if (src1_neg) {
        inst.opr.src1.flags |= RegisterFlags::Negative;
    }

    Operand pred_op{};
    pred_op.bank = RegisterBank::PREDICATE;
    pred_op.num = pdst_n;
    inst.opr.dest = pred_op;

    const spv::Id pred_result = vtst_impl(inst, pred, zero_test, sign_test, load_mask, false);

    store(inst.opr.dest, pred_result);
    return true;
}

bool USSETranslatorVisitor::vtstmsk(
    ExtPredicate pred,
    Imm1 skipinv,
    Imm1 onceonly,
    Imm1 syncstart,
    Imm1 dest_ext,
    Imm1 test_flag_2,
    Imm1 src1_ext,
    Imm1 src2_ext,
    Imm1 prec,
    Imm1 src2_vscomp,
    RepeatCount rpt_count,
    Imm2 sign_test,
    Imm2 zero_test,
    Imm1 test_crcomb_and,
    Imm2 tst_mask_type,
    Imm2 dest_bank,
    Imm2 src1_bank,
    Imm2 src2_bank,
    Imm7 dest_n,
    Imm1 test_wben,
    Imm2 alu_sel,
    Imm4 alu_op,
    Imm7 src1_n,
    Imm7 src2_n) {
    DataType load_data_type;
    Opcode test_op = decode_test_inst(alu_sel, alu_op, prec, load_data_type);
    Instruction inst;

    if (test_op == Opcode::INVALID) {
        LOG_ERROR("Unsupported comparision: {} {}", alu_sel, alu_op);
        return false;
    }

    inst.opcode = test_op;

    if (tst_mask_type == 3) {
        LOG_ERROR("Invalid test mask type: 3");
        return false;
    }

    const bool use_double_reg = alu_sel == 0;
    const uint8_t reg_bits = use_double_reg ? 8 : 7;
    // TODO: In some op, we don't need to load src2 e.g. rsq
    inst.opr.src1 = decode_src12(inst.opr.src1, src1_n, src1_bank, src1_ext, use_double_reg, reg_bits, m_second_program);
    inst.opr.src2 = decode_src12(inst.opr.src2, src2_n, src2_bank, src2_ext, use_double_reg, reg_bits, m_second_program);
    inst.opr.dest = decode_dest(inst.opr.dest, dest_n, dest_bank, dest_ext, use_double_reg, reg_bits, m_second_program);

    inst.opr.src1.type = load_data_type;
    inst.opr.src2.type = load_data_type;
    inst.opr.dest.type = load_data_type;

    inst.opr.src1.swizzle = SWIZZLE_CHANNEL_4_DEFAULT;
    inst.opr.src2.swizzle = (alu_sel == 0 && src2_vscomp) ? (Swizzle4 SWIZZLE_CHANNEL_4(X, X, X, X)) : (Swizzle4 SWIZZLE_CHANNEL_4_DEFAULT);
    inst.opr.dest.swizzle = SWIZZLE_CHANNEL_4_DEFAULT;

    if (test_flag_2) {
        inst.opr.src1.flags |= RegisterFlags::Negative;
    }

    if (!test_wben) {
        // TODO support it
        LOG_ERROR("Write disabled vmsktst is not supported yet.");
        return false;
    }

    // input is always 4 in case of a dot product instruction
    const bool is_vdp = (inst.opcode == Opcode::VDP || inst.opcode == Opcode::VF16DP);
    const bool output_4 = (alu_sel == 0 && tst_mask_type == 2);
    // Mask type 0 on the vector ALU: one byte per component of the four-wide test, packed
    // into the destination register (psp2cgc: "add.nezero.f32 o6.x, -pa2.xyzw, sa6.xxxx"
    // followed by a test of bit 8 of o6, the y component's byte).
    const bool byte_mask_4 = (alu_sel == 0 && tst_mask_type == 0 && !is_vdp && load_data_type == DataType::F32);

    // The instruction repeats like the others; the operand increments come from SMLSI,
    // the second source keeping its register (Sony's listing of a repeated "cmp.eq.u8
    // pa0.x, o4.x, sa21.x" shows pa1, o5 and sa21 again for the second iteration).
    const Instruction base_inst = inst;
    set_repeat_multiplier(1, 1, 1, 1);
    BEGIN_REPEAT(rpt_count)
    inst = base_inst;
    inst.opr.dest.num += repeat_increase[3][current_repeat];
    inst.opr.src1.num += repeat_increase[1][current_repeat];

    spv::Id pred_result = vtst_impl(inst, pred, zero_test, sign_test, (is_vdp || output_4 || byte_mask_4) ? 0b1111 : 0b1, true);

    if (byte_mask_4) {
        const spv::Id u32_type = m_b.makeUintType(32);
        spv::Id packed = m_b.makeUintConstant(0);
        for (int comp = 0; comp < 4; comp++) {
            const spv::Id cond = m_b.createCompositeExtract(pred_result, m_b.makeBoolType(), comp);
            const spv::Id byte = m_b.createTriOp(spv::OpSelect, u32_type, cond, m_b.makeUintConstant(0xFFu << (8 * comp)), m_b.makeUintConstant(0));
            packed = m_b.createBinOp(spv::OpBitwiseOr, u32_type, packed, byte);
        }
        inst.opr.dest.type = DataType::UINT32;
        store(inst.opr.dest, packed);
        continue;
    }

    spv::Id output_type;
    spv::Id zeros;
    spv::Id ones;
    switch (load_data_type) {
    case DataType::F32:
        // The mask is a bitmask, all ones for true, not the float 1.0: the compiler follows
        // "VADD.nezero.f32.f32 o6.x ..." with "SHL.ltzero.u32 i0.x o6.x 23", a test of bit 8
        // of the result, which 0x3f800000 never has set.
        output_type = m_b.makeUintType(32);
        if (output_4)
            output_type = m_b.makeVectorType(output_type, 4);
        zeros = utils::make_uniform_vector_from_type(m_b, output_type, 0u);
        ones = utils::make_uniform_vector_from_type(m_b, output_type, 0xFFFFFFFFu);
        inst.opr.dest.type = DataType::UINT32;
        break;
    case DataType::F16:
        output_type = type_f32;
        if (output_4)
            output_type = m_b.makeVectorType(output_type, 4);
        zeros = utils::make_uniform_vector_from_type(m_b, output_type, 0.0f);
        ones = utils::make_uniform_vector_from_type(m_b, output_type, 1.0f);
        break;
    default: {
        uint32_t ones_val = 1;
        bool is_signed = false;
        switch (load_data_type) {
        case DataType::INT8:
            is_signed = true;
            ones_val = 0x7F;
            break;
        case DataType::INT16:
            is_signed = true;
            ones_val = 0x7FFF;
            break;
        case DataType::INT32:
            is_signed = true;
            ones_val = 0x7FFFFFFF;
            break;
        case DataType::UINT8:
            ones_val = 0xFF;
            break;
        case DataType::UINT16:
            ones_val = 0XFFFF;
            break;
        case DataType::UINT32:
            ones_val = 0xFFFFFFFF;
            break;
        default:
            LOG_ERROR("Unexpected type {}", static_cast<int>(load_data_type));
            break;
        }
        output_type = is_signed ? m_b.makeIntType(32) : m_b.makeUintType(32);
        zeros = utils::make_uniform_vector_from_type(m_b, output_type, 0);
        ones = utils::make_uniform_vector_from_type(m_b, output_type, ones_val);
        break;
    }
    }

    pred_result = m_b.createOp(spv::OpSelect, output_type, { pred_result, ones, zeros });

    store(inst.opr.dest, pred_result);
    END_REPEAT()
    reset_repeat_multiplier();

    return true;
}

bool USSETranslatorVisitor::br(
    ExtPredicate pred,
    Imm1 syncend,
    bool exception,
    bool pwait,
    Imm1 sync_ext,
    bool nosched,
    bool br_monitor,
    bool save_link,
    Imm1 br_type,
    Imm1 any_inst,
    Imm1 all_inst,
    uint32_t br_off) {
    assert(false && "Unreachable");

    LOG_ERROR("Branch instruction should not be recompiled here!");
    return true;
}
