extern "C" {
#include "vpmu-arm-translate.h" // Interface header between QEMU and VPMU
#include "vpmu-arm-instset.h"   // Instruction Set
}

#include "vpmu.hpp" // VPMU common headers
#include "Cortex-A9.hpp"
#include "vpmu-utils.hpp"

#ifdef CONFIG_VPMU_VFP
void CPU_CortexA9::Translation::_vfp_lock_release(int insn)
{
    int i;
    /*for(i = 0; i < 32; i++){
        vfp_locks[i] -= arm_vfp_instr_time[insn];
        if (vfp_locks[i] < 0)
            vfp_locks[i] = 0;
    }*/
    if (insn < ARM_VFP_INSTRUCTION_TOTAL_COUNTS)
        for (i = 0; i < 32; i++) {
            if (vfp_locks[i] <= arm_vfp_instr_time[insn])
                vfp_locks[i] = 0;
            else
                vfp_locks[i] -= arm_vfp_instr_time[insn];
        }
    // fprintf(stderr,"%s: rd[14]add=%d\n",__func__,vfp_locks[14]);
}

// tianman
void CPU_CortexA9::Translation::_vfp_lock_analyze(
  int rd, int rn, int rm, int dp, int insn)
{
    int rd1, rd2;
    int rn1, rn2;
    int rm1, rm2;
    int max = 0;
    int latency;
    int i;

    // latency = arm_vfp_latency[insn] - arm_vfp_instr_time[insn];
    latency = arm_vfp_latency[insn] - 1;

    if (dp) {
        rd1 = rd * 2;
        rd2 = rd1 + 1;

        rn1 = rn * 2;
        rn2 = rn1 + 1;

        rm1 = rm * 2;
        rm2 = rm1 + 1;
    }

    if (dp) {
        // if(vfp_locks[rd1]>0 || vfp_locks[rd2]>0 || vfp_locks[rn1]>0 ||
        //   vfp_locks[rn2]>0 || vfp_locks[rm1]>0 || vfp_locks[rm2]>0)
        if (vfp_locks[rd1] > max) max = vfp_locks[rd1];
        if (vfp_locks[rd2] > max) max = vfp_locks[rd2];
        if (vfp_locks[rn1] > max) max = vfp_locks[rn1];
        if (vfp_locks[rn2] > max) max = vfp_locks[rn2];
        if (vfp_locks[rm1] > max) max = vfp_locks[rm1];
        if (vfp_locks[rm2] > max) max = vfp_locks[rm2];

        if (max > 0) {
            vfp_base += max;
            for (i = 0; i < 32; i++) {
                if (vfp_locks[i] <= max)
                    vfp_locks[i] = 0;
                else
                    vfp_locks[i] -= max;
            }

            /*for (i = 0; i < 32; i++) {
                vfp_locks[i] -= max;
                if (vfp_locks[i] < 0)
                    vfp_locks[i] = 0;
            }*/
        }

        // fprintf(stderr,"%s: rd[%d]add=%d\n",__func__,rd1,vfp_locks[rd1]);

        vfp_locks[rd1] += latency;
        vfp_locks[rd2] += latency;
        if (rn1 != rd1) {
            vfp_locks[rn1] += latency;
            vfp_locks[rn2] += latency;
        }
        if (rm1 != rd1 && rm1 != rn1) {
            vfp_locks[rm1] += latency;
            vfp_locks[rm2] += latency;
        }

        // fprintf(stderr,"%s: rd1= %d, rd1[%d]=%d, rd2=%d,rn1=%d,rn2=%d,rm1=%d,rm2=%d
        // max=%d latency
        // =%d\n",__func__,rd1,rd1,vfp_locks[rd1],rd2,rn1,rn2,rm1,rm2,max,latency);

    } else {
        // if(vfp_locks[rd]>0 || vfp_locks[rn]>0 || vfp_locks[rm]>0)
        if (vfp_locks[rd] > max) max = vfp_locks[rd];
        if (vfp_locks[rn] > max) max = vfp_locks[rn];
        if (vfp_locks[rm] > max) max = vfp_locks[rm];
        if (max > 0) {
            vfp_base += max;
            /*for(i = 0; i < 32; i++){
                vfp_locks[i] -= max;
                if (vfp_locks[i] < 0)
                    vfp_locks[i] = 0;
            }*/
            for (i = 0; i < 32; i++) {
                if (vfp_locks[i] <= max)
                    vfp_locks[i] = 0;
                else
                    vfp_locks[i] -= max;
            }
        }

        vfp_locks[rd] += latency;
        if (rn != rd) {
            vfp_locks[rn] += latency;
        }
        if (rm != rd && rm != rn) {
            vfp_locks[rm] += latency;
        }
    }
}

/*
 *  Implement by Tianman
 *  Simulate the VFP unit.
 */
int CPU_CortexA9::Translation::_analyze_vfp_ticks(uint32_t insn, uint64_t vfp_vec_len)
{

    uint32_t rd, rn, rm, op, i, n, offset, delta_d, delta_m, bank_mask;
    int      dp, veclen;

    dp = ((insn & 0xf00) == 0xb00);
    switch ((insn >> 24) & 0xf) {
    case 0xe:
        if (insn & (1 << 4)) {
            /* single register transfer */
            rd = (insn >> 12) & 0xf;
            if (dp) {
                int size;
                int pass;

                VFP_DREG_N(rn, insn);
                if (insn & 0xf) return 1;

                pass = (insn >> 21) & 1;
                if (insn & (1 << 22)) {
                    size   = 0;
                    offset = ((insn >> 5) & 3) * 8;
                } else if (insn & (1 << 5)) {
                    size   = 1;
                    offset = (insn & (1 << 6)) ? 16 : 0;
                } else {
                    size   = 2;
                    offset = 0;
                }
                if (insn & ARM_CP_RW_BIT) {
                    /* vfp->arm */
                    switch (size) {
                    case 0:
                        break;
                    case 1:
                        break;
                    case 2:
                        /*Chritine ADD*/
                        // We want to know total vfp instruction count.
                        // vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1;

                        // tianman
                        if (pass) {
                            // vfp_count[ARM_VFP_INSTRUCTION_FMRDH]+=1;
                                    _vfp_lock_release(ARM_VFP_INSTRUCTION_FMRDH,&();
                        } else {
                            // vfp_count[ARM_VFP_INSTRUCTION_FMRDL]+=1;
                                    _vfp_lock_release(ARM_VFP_INSTRUCTION_FMRDL,&();
                        }
                        break;
                    }
                } else {
                    /* arm->vfp */
                    if (insn & (1 << 23)) {
                        /* VDUP */
                    } else {
                        /* VMOV */
                        switch (size) {
                        case 0:
                            break;
                        case 1:
                            break;
                        case 2:
                            // vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                            // tianman
                            if (pass) {
                                // vfp_count[ARM_VFP_INSTRUCTION_FMDHR]+=1;
                                        _vfp_lock_release(ARM_VFP_INSTRUCTION_FMDHR,&();
                            } else {
                                // vfp_count[ARM_VFP_INSTRUCTION_FMDLR]+=1;
                                        _vfp_lock_release(ARM_VFP_INSTRUCTION_FMDLR,&();
                            }
                            break;
                        }
                        // neon_store_reg(rn, pass, tmp);
                    }
                }
            } else { /* !dp */
                if ((insn & 0x6f) != 0x00) return 1;
                rn = VFP_SREG_N(insn);
                if (insn & ARM_CP_RW_BIT) {
                    /* vfp->arm */
                    if (insn & (1 << 21)) {
                        // vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                        // vfp_count[ARM_VFP_INSTRUCTION_FMRX]+= 1;//tianman
                            _vfp_lock_release(ARM_VFP_INSTRUCTION_FMRX,&();

                            /* system register */
                            rn >>= 1;

                            switch (rn) {
                        case ARM_VFP_FPSID:
                            /* VFP2 allows access to FSID from userspace.
                               VFP3 restricts all id registers to privileged
                               accesses.  */
                            break;
                        case ARM_VFP_FPEXC:
                            if (IS_USER(s)) return 1;
                            break;
                        case ARM_VFP_FPINST:
                        case ARM_VFP_FPINST2:
                            /* Not present in VFP3.  */
                            break;
                        case ARM_VFP_FPSCR:
                            if (rd == 15) {
                                // vfp_count[ARM_VFP_INSTRUCTION_FMSTAT]+= 1;//tianman
                                // vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1;
                                // //Christine
                                        _vfp_lock_release(ARM_VFP_INSTRUCTION_FMSTAT,&();
                            } else {
                            }
                            break;
                        case ARM_VFP_MVFR0:
                        case ARM_VFP_MVFR1:
                            break;
                        default:
                            return 1;
                            }
                    } else {
                        // vfp_count[ARM_VFP_INSTRUCTION_FMRS]+= 1;//tianman
                        // vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                            _vfp_lock_release(ARM_VFP_INSTRUCTION_FMRS,&();
                    }
                    if (rd == 15) {
                        /* Set the 4 flag bits in the CPSR.  */
                    } else {
                    }
                } else {
                    /* arm->vfp */
                    if (insn & (1 << 21)) {
                        // vfp_count[ARM_VFP_INSTRUCTION_FMXR]+= 1;//tianman
                        // vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                            _vfp_lock_release(ARM_VFP_INSTRUCTION_FMXR,&();
                            rn >>= 1;
                            /* system register */
                            switch (rn) {
                        case ARM_VFP_FPSID:
                        case ARM_VFP_MVFR0:
                        case ARM_VFP_MVFR1:
                            /* Writes are ignored.  */
                            break;
                        case ARM_VFP_FPSCR:
                            break;
                        case ARM_VFP_FPEXC:
                            if (IS_USER(s)) return 1;
                            break;
                        case ARM_VFP_FPINST:
                        case ARM_VFP_FPINST2:
                            break;
                        default:
                            return 1;
                            }
                    } else {
                        // vfp_count[ARM_VFP_INSTRUCTION_FMSR]+= 1;//tianman
                        // vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                            _vfp_lock_release(ARM_VFP_INSTRUCTION_FMSR,&();
                    }
                }
            }
        } else {
            /* data processing */
            /* The opcode is in bits 23, 21, 20 and 6.  */
            op = ((insn >> 20) & 8) | ((insn >> 19) & 6) | ((insn >> 6) & 1);
            if (dp) {
                if (op == 15) {
                    /* rn is opcode */
                    rn = ((insn >> 15) & 0x1e) | ((insn >> 7) & 1);
                } else {
                    /* rn is register number */
                    VFP_DREG_N(rn, insn);
                }

                if (op == 15 && (rn == 15 || rn > 17)) {
                    /* Integer or single precision destination.  */
                    rd = VFP_SREG_D(insn);
                } else {
                    VFP_DREG_D(rd, insn);
                }

                if (op == 15 && (rn == 16 || rn == 17)) {
                    /* Integer source.  */
                    rm = ((insn << 1) & 0x1e) | ((insn >> 5) & 1);
                } else {
                    VFP_DREG_M(rm, insn);
                }
            } else {
                rn = VFP_SREG_N(insn);
                if (op == 15 && rn == 15) {
                    /* Double precision destination.  */
                    VFP_DREG_D(rd, insn);
                } else {
                    rd = VFP_SREG_D(insn);
                }
                rm = VFP_SREG_M(insn);
            }

            // fprintf(stderr,"%s:rd=%d rm=%d rn=%d dp=%d\n",__func__,rd,rm,rn,dp);

            veclen = vfp_vec_len;
            // Christine : Decrease the fprintf.
            //	fprintf(stderr,"%s:veclen=%d\n",__func__,veclen);
            if (op == 15 && rn > 3) veclen = 0;

            /* Shut up compiler warnings.  */
            delta_m   = 0;
            delta_d   = 0;
            bank_mask = 0;

            /* Load the initial operands.  */
            if (op == 15) {
                switch (rn) {
                case 16:
                case 17:
                    /* Integer source */
                    break;
                case 8:
                case 9:
                    /* Compare */
                    break;
                case 10:
                case 11:
                    /* Compare with zero */
                    break;
                case 20:
                case 21:
                case 22:
                case 23:
                case 28:
                case 29:
                case 30:
                case 31:
                    /* Source and destination the same.  */
                    break;
                default:
                    /* One source operand.  */
                    break;
                }
            } else {
                /* Two source operands.  */
            }

            for (;;) {
                /* Perform the calculation.  */
                switch (op) {
                case 0: /* mac: fd + (fn * fm) */
                        // vfp_count[ARM_VFP_INSTRUCTION_FMACD + (1-dp) ]+= 1;//tianman
                        // vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                            _vfp_lock_analyze(rd, rn, rm, dp, ARM_VFP_INSTRUCTION_FMACD + (1-dp),&();
                            _vfp_lock_release(ARM_VFP_INSTRUCTION_FMACD + (1-dp),&();
                            break;
                        case 1: /* nmac: fd - (fn * fm) */
                            //vfp_count[ARM_VFP_INSTRUCTION_FNMACD + (1-dp) ]+= 1;//tianman
                            //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                            _vfp_lock_analyze(rd, rn, rm, dp, ARM_VFP_INSTRUCTION_FNMACD + (1-dp),&( );
                            _vfp_lock_release(ARM_VFP_INSTRUCTION_FNMACD + (1-dp),&();
                            break;
                        case 2: /* msc: -fd + (fn * fm) */
                            //vfp_count[ARM_VFP_INSTRUCTION_FMSCD + (1-dp) ]+= 1;//tianman
                            //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                            _vfp_lock_analyze(rd, rn, rm, dp, ARM_VFP_INSTRUCTION_FMSCD + (1-dp),&( );
                            _vfp_lock_release(ARM_VFP_INSTRUCTION_FMSCD + (1-dp),&();
                            break;
                        case 3: /* nmsc: -fd - (fn * fm)  */
                            //vfp_count[ARM_VFP_INSTRUCTION_FNMSCD + (1-dp) ]+= 1;//tianman
                            //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                            _vfp_lock_analyze(rd, rn, rm, dp, ARM_VFP_INSTRUCTION_FNMSCD + (1-dp),&( );
                            _vfp_lock_release(ARM_VFP_INSTRUCTION_FNMSCD + (1-dp),&();
                            break;
                        case 4: /* mul: fn * fm */
                            //vfp_count[ARM_VFP_INSTRUCTION_FMULD + (1-dp) ]+= 1;//tianman
                            //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                            _vfp_lock_analyze(rd, rn, rm, dp, ARM_VFP_INSTRUCTION_FMULD + (1-dp),&( );
                            _vfp_lock_release(ARM_VFP_INSTRUCTION_FMULD + (1-dp),&();
                            break;
                        case 5: /* nmul: -(fn * fm) */
                            //vfp_count[ARM_VFP_INSTRUCTION_FNMULD + (1-dp) ]+= 1;//tianman
                            //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                            _vfp_lock_analyze(rd, rn, rm, dp, ARM_VFP_INSTRUCTION_FNMULD + (1-dp),&( );
                            _vfp_lock_release(ARM_VFP_INSTRUCTION_FNMULD + (1-dp),&();
                            break;
                        case 6: /* add: fn + fm */
                            //vfp_count[ARM_VFP_INSTRUCTION_FADDD + (1-dp) ]+= 1;//tianman
                            //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                            _vfp_lock_analyze(rd, rn, rm, dp, ARM_VFP_INSTRUCTION_FADDD + (1-dp),&( );
                            _vfp_lock_release(ARM_VFP_INSTRUCTION_FADDD + (1-dp),&();
                            break;
                        case 7: /* sub: fn - fm */
                            //vfp_count[ARM_VFP_INSTRUCTION_FSUBD + (1-dp) ]+= 1;//tianman
                            //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                            _vfp_lock_analyze(rd, rn, rm, dp, ARM_VFP_INSTRUCTION_FSUBD + (1-dp) ,&();
                            _vfp_lock_release(ARM_VFP_INSTRUCTION_FSUBD + (1-dp),&();
                            break;
                        case 8: /* div: fn / fm */
                            //vfp_count[ARM_VFP_INSTRUCTION_FDIVD + (1-dp) ]+= 1;//tianman
                            //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                            // _vfp_lock_analyze(rd, rn, rm, dp, ARM_VFP_INSTRUCTION_FDIVD + (1-dp),&( );
                            _vfp_lock_release(ARM_VFP_INSTRUCTION_FDIVD + (1-dp),&();
                            break;
                        case 14: /* fconst */

                            n = (insn << 12) & 0x80000000;
                            i = ((insn >> 12) & 0x70) | (insn & 0xf);
                            if (dp) {
                        if (i & 0x40)
                            i |= 0x3f80;
                        else
                            i |= 0x4000;
                        n |= i << 16;
                            } else {
                        if (i & 0x40)
                            i |= 0x780;
                        else
                            i |= 0x800;
                        n |= i << 19;
                            }
                            break;
                        case 15: /* extension space */
                            switch (rn) {
                    case 0: /* cpy */
                        // vfp_count[ARM_VFP_INSTRUCTION_FCPYD + (1-dp) ]+= 1;//tianman
                        // vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                                    _vfp_lock_analyze(rd, rd, rm, dp, ARM_VFP_INSTRUCTION_FDIVD + (1-dp),&( );
                                    _vfp_lock_release(ARM_VFP_INSTRUCTION_FCPYD + (1-dp),&();
                                    /* no-op */
                                    break;
                                case 1: /* abs */
                                    //vfp_count[ARM_VFP_INSTRUCTION_FABSD + (1-dp) ]+= 1;//tianman
                                    //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                                    _vfp_lock_analyze(rd, rd, rm, dp, ARM_VFP_INSTRUCTION_FDIVD + (1-dp),&( );
                                    _vfp_lock_release(ARM_VFP_INSTRUCTION_FABSD + (1-dp),&();
                                    break;
                                case 2: /* neg */
                                    //vfp_count[ARM_VFP_INSTRUCTION_FNEGD + (1-dp) ]+= 1;//tianman
                                    //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                                    _vfp_lock_analyze(rd, rd, rm, dp, ARM_VFP_INSTRUCTION_FDIVD + (1-dp),&( );
                                    _vfp_lock_release(ARM_VFP_INSTRUCTION_FNEGD + (1-dp),&();
                                    break;
                                case 3: /* sqrt */
                                    //vfp_count[ARM_VFP_INSTRUCTION_FSQRTD + (1-dp) ]+= 1;//tianman
                                    //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                                    // _vfp_lock_analyze(rd, rd, rm, dp, ARM_VFP_INSTRUCTION_FDIVD + (1-dp),&( );
                                    _vfp_lock_release(ARM_VFP_INSTRUCTION_FSQRTD + (1-dp),&();
                                    break;
                                case 8: /* cmp */
                                    //vfp_count[ARM_VFP_INSTRUCTION_FCMPD + (1-dp) ]+= 1;//tianman
                                    //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                                    _vfp_lock_analyze(rd, rd, rm, dp, ARM_VFP_INSTRUCTION_FDIVD + (1-dp),&( );
                                    _vfp_lock_release(ARM_VFP_INSTRUCTION_FCMPD + (1-dp),&();
                                    break;
                                case 9: /* cmpe */
                                    //vfp_count[ARM_VFP_INSTRUCTION_FCMPED + (1-dp) ]+= 1;//tianman
                                    //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                                    _vfp_lock_analyze(rd, rd, rm, dp, ARM_VFP_INSTRUCTION_FDIVD + (1-dp),&( );
                                    _vfp_lock_release(ARM_VFP_INSTRUCTION_FCMPED + (1-dp),&();
                                    break;
                                case 10: /* cmpz */
                                    //vfp_count[ARM_VFP_INSTRUCTION_FCMPZD + (1-dp) ]+= 1;//tianman
                                    //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                                    _vfp_lock_analyze(rd, rd, rd, dp, ARM_VFP_INSTRUCTION_FDIVD + (1-dp),&( );
                                    _vfp_lock_release(ARM_VFP_INSTRUCTION_FCMPZD + (1-dp),&();
                                    break;
                                case 11: /* cmpez */
                                    //vfp_count[ARM_VFP_INSTRUCTION_FCMPEZD + (1-dp) ]+= 1;//tianman
                                    //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                                    _vfp_lock_analyze(rd, rd, rd, dp, ARM_VFP_INSTRUCTION_FDIVD + (1-dp),&( );
                                    _vfp_lock_release(ARM_VFP_INSTRUCTION_FCMPEZD + (1-dp),&();
                                    break;
                                case 15: /* single<->double conversion */
                                    if (dp){
                            // vfp_count[ARM_VFP_INSTRUCTION_FCVTSD]+= 1;//tianman
                            // vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                                        _vfp_lock_release(ARM_VFP_INSTRUCTION_FCVTSD + (1-dp),&();
                                    }
                                    else{
                            // vfp_count[ARM_VFP_INSTRUCTION_FCVTDS]+= 1;//tianman
                            // vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                                        _vfp_lock_release(ARM_VFP_INSTRUCTION_FCVTDS + (1-dp),&();
                                    }
                                    break;
                                case 16: /* fuito */
                                    //vfp_count[ARM_VFP_INSTRUCTION_FUITOD + (1-dp) ]+= 1;//tianman
                                    //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                                    _vfp_lock_release(ARM_VFP_INSTRUCTION_FUITOD + (1-dp),&();
                                    break;
                                case 17: /* fsito */
                                    //vfp_count[ARM_VFP_INSTRUCTION_FSITOD + (1-dp) ]+= 1;//tianman
                                    //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                                    _vfp_lock_release(ARM_VFP_INSTRUCTION_FSITOD + (1-dp),&();
                                    break;
                                case 20: /* fshto */
                                    break;
                                case 21: /* fslto */
                                    break;
                                case 22: /* fuhto */
                                    break;
                                case 23: /* fulto */
                                    break;
                                case 24: /* ftoui */
                                    //vfp_count[ARM_VFP_INSTRUCTION_FTOUID + (1-dp) ]+= 1;//tianman
                                    //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                                    _vfp_lock_release(ARM_VFP_INSTRUCTION_FTOUID + (1-dp),&();
                                    break;
                                case 25: /* ftouiz */
                                    //vfp_count[ARM_VFP_INSTRUCTION_FTOUIZD + (1-dp) ]+= 1;//tianman
                                    //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                                    _vfp_lock_release(ARM_VFP_INSTRUCTION_FTOUIZD + (1-dp),&();
                                    break;
                                case 26: /* ftosi */
                                    //vfp_count[ARM_VFP_INSTRUCTION_FTOSID + (1-dp) ]+= 1;//tianman
                                    //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                                    _vfp_lock_release(ARM_VFP_INSTRUCTION_FTOSID + (1-dp),&();
                                    break;
                                case 27: /* ftosiz */
                                    //vfp_count[ARM_VFP_INSTRUCTION_FTOUSIZD + (1-dp) ]+= 1;//tianman
                                    //vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                                    _vfp_lock_release(ARM_VFP_INSTRUCTION_FTOUSIZD + (1-dp),&();
                                    break;
                                case 28: /* ftosh */
                                    break;
                                case 29: /* ftosl */
                                    break;
                                case 30: /* ftouh */
                                    break;
                                case 31: /* ftoul */
                                    break;
                                default: /* undefined */
                                    printf ("rn:%d\n", rn);
                                    return 1;
                            }
                            break;
                        default: /* undefined */
                            printf ("op:%d\n", op);
                            return 1;
                }

                /* Write back the result.  */
                if (op == 15 && (rn >= 8 && rn <= 11)) {
                } /* Comparison, do nothing.  */
                else if (op == 15 && rn > 17) {
                } /* Integer result.  */
                else if (op == 15 && rn == 15) {
                } /* conversion */
                else {
                }
                /* break out of the loop if we have finished  */
                if (veclen == 0) break;

                if (op == 15 && delta_m == 0) {
                    /* single source one-many */
                    while (veclen--) {
                        rd = ((rd + delta_d) & (bank_mask - 1)) | (rd & bank_mask);
                    }
                    break;
                }
                /* Setup the next operands.  */
                veclen--;
                rd = ((rd + delta_d) & (bank_mask - 1)) | (rd & bank_mask);

                if (op == 15) {
                    /* One source operand.  */
                    rm = ((rm + delta_m) & (bank_mask - 1)) | (rm & bank_mask);
                } else {
                    /* Two source operands.  */
                    rn = ((rn + delta_d) & (bank_mask - 1)) | (rn & bank_mask);
                    if (delta_m) {
                        rm = ((rm + delta_m) & (bank_mask - 1)) | (rm & bank_mask);
                    }
                }
            }
        }
        break;
    case 0xc:
    case 0xd:
        if (dp && (insn & 0x03e00000) == 0x00400000) {
            /* two-register transfer */
            rn = (insn >> 16) & 0xf;
            rd = (insn >> 12) & 0xf;
            if (dp) {
                VFP_DREG_M(rm, insn);
            } else {
                rm = VFP_SREG_M(insn);
            }

            if (insn & ARM_CP_RW_BIT) {
                /* vfp->arm */
                if (dp) {
                    // vfp_count[ARM_VFP_INSTRUCTION_FMRRD]+= 1;//tianman
                    // vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                        _vfp_lock_release(ARM_VFP_INSTRUCTION_FMRRD,&();
                } else {
                    // vfp_count[ARM_VFP_INSTRUCTION_FMRRS]+= 1;//tianman
                        _vfp_lock_release(ARM_VFP_INSTRUCTION_FMRRS,&();
                }
            } else {
                /* arm->vfp */
                if (dp) {
                    // vfp_count[ARM_VFP_INSTRUCTION_FMDRR]+= 1;//tianman
                    // vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                        _vfp_lock_release(ARM_VFP_INSTRUCTION_FMDRR,&();
                } else {
                    // vfp_count[ARM_VFP_INSTRUCTION_FMSRR]+= 1;//tianman
                    // vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                        _vfp_lock_release(ARM_VFP_INSTRUCTION_FMSRR,&();
                }
            }
        } else {
            /* Load/store */
            rn = (insn >> 16) & 0xf;
            if (dp)
                VFP_DREG_D(rd, insn);
            else
                rd = VFP_SREG_D(insn);
            if (s->thumb && rn == 15) {
            } else {
            }
            if ((insn & 0x01200000) == 0x01000000) {
                /* Single load/store */
                offset                              = (insn & 0xff) << 2;
                if ((insn & (1 << 23)) == 0) offset = -offset;
                if (insn & (1 << 20)) {
                    // vfp_count[ARM_VFP_INSTRUCTION_FLDD + (1-dp)]+= 1;//tianman
                    // vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                        _vfp_lock_release(ARM_VFP_INSTRUCTION_FLDD + (1-dp),&();
                } else {
                    // vfp_count[ARM_VFP_INSTRUCTION_FSTD + (1-dp)]+= 1;//tianman
                    // vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                        _vfp_lock_release(ARM_VFP_INSTRUCTION_FSTD + (1-dp),&();
                }
            } else {
                /* load/store multiple */
                if (dp)
                    n = (insn >> 1) & 0x7f;
                else
                    n = insn & 0xff;

                if (insn & (1 << 24)) /* pre-decrement */
                {
                }

                if (dp)
                    offset = 8;
                else
                    offset = 4;

                for (i = 0; i < n; i++) {
                    if (insn & ARM_CP_RW_BIT) {
                        /* load */
                        if (insn & 0x1) {
                            // vfp_count[ARM_VFP_INSTRUCTION_FLDMX]+= 1;//tianman
                            // vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                                _vfp_lock_release(ARM_VFP_INSTRUCTION_FLDMX,&();
                        } else {
                            // vfp_count[ARM_VFP_INSTRUCTION_FLDMD + (1-dp)]+= 1;//tianman
                            // vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                                _vfp_lock_release(ARM_VFP_INSTRUCTION_FLDMD + (1-dp),&();
                        }
                    } else {
                        /* store */

                        if (insn & 0x1) {
                            // vfp_count[ARM_VFP_INSTRUCTION_FSTMX]+= 1;//tianman
                            // vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                                _vfp_lock_release(ARM_VFP_INSTRUCTION_FSTMX,&();
                        } else {
                            // vfp_count[ARM_VFP_INSTRUCTION_FSTMD + (1-dp)]+= 1;//tianman
                            // vfp_count[ARM_VFP_INSTRUCTION_TOTAL_COUNT]+=1; //Christine
                                _vfp_lock_release(ARM_VFP_INSTRUCTION_FSTMD + (1-dp),&();
                        }
                    }
                }
                if (insn & (1 << 21)) {
                    /* writeback */
                    if (insn & (1 << 24))
                        offset = -offset * n;
                    else if (dp && (insn & 1))
                        offset = 4;
                    else
                        offset = 0;

                    if (offset != 0) {
                    }
                }
            }
        }
        break;
    default:
        /* Should never happen.  */
        return 1;
    }
    return 0;
}
#endif // CONFIG_VPMU_VFP

#if 0
//TODO This code might be useful someday??
/*
 *  Implement by Tianman
 *	Simulate the EX stage in pipe line, without interlock.
 *  Accumulate the cycle count in each instruction counter in each TB.
 */
static int analyze_arm_ticks(uint32_t insn, CPUARMState *env, DisasContext *s)
{

    unsigned int cond, op1, shift, rn, rd, sh;
    unsigned int instr_index = 0;//tianman

    cond = insn >> 28;
    if (cond == 0xf){
        /* Unconditional instructions.  */
        if (((insn >> 25) & 7) == 1) {
            /* NEON Data processing.  */
            //arm_count[ARM_INSTRUCTION_NEON_DP]+=1;
            return 0;
        }
        if ((insn & 0x0f100000) == 0x04000000) {
            /* NEON load/store.  */
            //arm_count[ARM_INSTRUCTION_NEON_LS]+=1;
            return 0;
        }
        if ((insn & 0x0d70f000) == 0x0550f000)
        {
            //arm_count[ARM_INSTRUCTION_PLD]+=1;
            return 0; /* PLD */
        }
        else if ((insn & 0x0ffffdff) == 0x01010000) {
            //arm_count[ARM_INSTRUCTION_SETEND]+=1;//tianman
            /* setend */
            return 0;
        } else if ((insn & 0x0fffff00) == 0x057ff000) {
            switch ((insn >> 4) & 0xf) {
                case 1: /* clrex */
                    //arm_count[ARM_INSTRUCTION_CLREX]+=1;//tianman
                    return 0;
                case 4: /* dsb */
                    //arm_count[ARM_INSTRUCTION_DSB]+=1;//tianman
                case 5: /* dmb */
                    //arm_count[ARM_INSTRUCTION_DMB]+=1;//tianman
                case 6: /* isb */
                    //arm_count[ARM_INSTRUCTION_ISB]+=1;//tianman
                    /* We don't emulate caches so these are a no-op.  */
                    return 0;
                default:
                    goto illegal_op;
            }
        } else if ((insn & 0x0e5fffe0) == 0x084d0500) {
            /* srs */
            //arm_count[ARM_INSTRUCTION_SRS]+=1;//tianman
            return 0;
        } else if ((insn & 0x0e5fffe0) == 0x081d0a00) {
            /* rfe */
            //arm_count[ARM_INSTRUCTION_RFE]+=1;//tianman
            return 0;
        } else if ((insn & 0x0e000000) == 0x0a000000) {
            /* branch link and change to thumb (blx <offset>) */
            //arm_count[ARM_INSTRUCTION_BLX]+=1;//tianman
            return 0;
        } else if ((insn & 0x0e000f00) == 0x0c000100) {
            //LDC,STC?
            if (insn & (1 << 20)) {
                //arm_count[ARM_INSTRUCTION_LDC]+=1;//tianman
            }
            else{
                //arm_count[ARM_INSTRUCTION_STC]+=1;//tianman
            }
            return 0;
        } else if ((insn & 0x0fe00000) == 0x0c400000) {
            /* Coprocessor double register transfer.  */
            //MCRR, MRRC
            if (insn & (1 << 20)) {
                //arm_count[ARM_INSTRUCTION_MRRC]+=1;//tianman
            }
            else{
                //arm_count[ARM_INSTRUCTION_MCRR]+=1;//tianman
            }
            return 0;
        } else if ((insn & 0x0f000010) == 0x0e000010) {
            /* Additional coprocessor register transfer.  */
            //MCR,MRC
            if (insn & (1 << 20)) {
                //arm_count[ARM_INSTRUCTION_MRC]+=1;//tianman
            }
            else{
                //arm_count[ARM_INSTRUCTION_MCR]+=1;//tianman
            }
            return 0;
        } else if ((insn & 0x0ff10020) == 0x01000000) {
            /* cps (privileged) */
            //arm_count[ARM_INSTRUCTION_CPS]+=1;//tianman
            return 0;
        }
        goto illegal_op;
    }

    if ((insn & 0x0f900000) == 0x03000000) {
        if ((insn & (1 << 21)) == 0) {
            if ((insn & (1 << 22)) == 0) {
                /* MOVW */
                //arm_count[ARM_INSTRUCTION_MOVW]+=1;//tianman
            } else {
                /* MOVT */
                //arm_count[ARM_INSTRUCTION_MOVT]+=1;//tianman
            }
        } else {
            if (((insn >> 16) & 0xf) == 0) {
            } else {
                /* CPSR = immediate */
                //arm_count[ARM_INSTRUCTION_MSR]+=1;//tianman
            }
        }
    } else if ((insn & 0x0f900000) == 0x01000000
            && (insn & 0x00000090) != 0x00000090) {
        /* miscellaneous instructions */
        op1 = (insn >> 21) & 3;
        sh = (insn >> 4) & 0xf;
        switch (sh) {
            case 0x0: /* move program status register */
                if (op1 & 1) {
                    /* PSR = reg */
                    //arm_count[ARM_INSTRUCTION_MSR]+=1;//tianman
                } else {
                    /* reg = PSR */
                    //arm_count[ARM_INSTRUCTION_MRS]+=1;//tianman
                }
                break;
            case 0x1:
                if (op1 == 1) {
                    //arm_count[ARM_INSTRUCTION_BX]+=1;//tianman
                    /* branch/exchange thumb (bx).  */
                } else if (op1 == 3) {
                    /* clz */
                    //arm_count[ARM_INSTRUCTION_CLZ]+=1;//tianman
                } else {
                    goto illegal_op;
                }
                break;
            case 0x2:
                if (op1 == 1) {
                    //arm_count[ARM_INSTRUCTION_BXJ]+=1;//tianman
                } else {
                    goto illegal_op;
                }
                break;
            case 0x3:
                if (op1 != 1)
                    goto illegal_op;
                //arm_count[ARM_INSTRUCTION_BLX]+=1;//tianman

                /* branch link/exchange thumb (blx) */
                break;
            case 0x5: /* saturating add/subtract */
                if (op1 & 2){
                    if (op1 & 1)
                        //arm_count[ARM_INSTRUCTION_QDSUB]+=1;//tianman
                    else
                        //arm_count[ARM_INSTRUCTION_QDADD]+=1;//tianman
                }
                if (op1 & 1){
                    //arm_count[ARM_INSTRUCTION_QSUB]+=1;//tianman
                }
                else{
                    //arm_count[ARM_INSTRUCTION_QADD]+=1;//tianman
                }
                break;
            case 7: /* bkpt */
                //arm_count[ARM_INSTRUCTION_BKPT]+=1;//tianman
                break;
            case 0x8: /* signed multiply */
            case 0xa:
            case 0xc:
            case 0xe:
                if (op1 == 1) {
                    /* (32 * 16) >> 16 */
                    if ((sh & 2) == 0) {
                        //arm_count[ARM_INSTRUCTION_SMLAWY]+=1;//tianman
                    }
                    //arm_count[ARM_INSTRUCTION_SMULWY]+=1;//tianman
                } else {
                    /* 16 * 16 */
                    if(op1 == 3)
                        //arm_count[ARM_INSTRUCTION_SMULXY]+=1;//tianman
                    if (op1 == 2) {
                        //arm_count[ARM_INSTRUCTION_SMLALXY]+=1;//tianman
                    } else {
                        if (op1 == 0) {
                            //arm_count[ARM_INSTRUCTION_SMLAXY]+=1;//tianman
                        }
                    }
                }
                break;
            default:
                goto illegal_op;
        }
    } else if (((insn & 0x0e000000) == 0 &&
                (insn & 0x00000090) != 0x90) ||
            ((insn & 0x0e000000) == (1 << 25))) {
#if 0
        int set_cc, logic_cc, shiftop;

        set_cc = (insn >> 20) & 1;
        logic_cc = table_logic_cc[op1] & set_cc;

        /* data processing instruction */
        if (insn & (1 << 25)) {
            /* immediate operand */
            val = insn & 0xff;
            shift = ((insn >> 8) & 0xf) * 2;
            if (shift) {
                val = (val >> shift) | (val << (32 - shift));
            }
            tmp2 = new_tmp();
            tcg_gen_movi_i32(tmp2, val);
            if (logic_cc && shift) {
                gen_set_CF_bit31(tmp2);
            }
        } else {
            /* register */
            rm = (insn) & 0xf;
            tmp2 = load_reg(s, rm);
            shiftop = (insn >> 5) & 3;
            if (!(insn & (1 << 4))) {
                shift = (insn >> 7) & 0x1f;
                gen_arm_shift_im(tmp2, shiftop, shift, logic_cc);
            } else {
                rs = (insn >> 8) & 0xf;
                tmp = load_reg(s, rs);
                gen_arm_shift_reg(tmp2, shiftop, tmp, logic_cc);
            }
        }
        if (op1 != 0x0f && op1 != 0x0d) {
            rn = (insn >> 16) & 0xf;
            tmp = load_reg(s, rn);
        } else {
            TCGV_UNUSED(tmp);
        }
#endif
        op1 = (insn >> 21) & 0xf;
        rd = (insn >> 12) & 0xf;
        switch(op1) {
            case 0x00:
                //arm_count[ARM_INSTRUCTION_AND]+=1;//tianman
                break;
            case 0x01:
                //arm_count[ARM_INSTRUCTION_EOR]+=1;//tianman
                break;
            case 0x02:
                //arm_count[ARM_INSTRUCTION_SUB]+=1;//tianman
                break;
            case 0x03:
                //arm_count[ARM_INSTRUCTION_RSB]+=1;//tianman
                break;
            case 0x04:
                //arm_count[ARM_INSTRUCTION_ADD]+=1;//tianman
                break;
            case 0x05:
                //arm_count[ARM_INSTRUCTION_ADC]+=1;//tianman
                break;
            case 0x06:
                //arm_count[ARM_INSTRUCTION_SBC]+=1;//tianman
                break;
            case 0x07:
                //arm_count[ARM_INSTRUCTION_RSC]+=1;//tianman
                break;
            case 0x08:
                //arm_count[ARM_INSTRUCTION_TST]+=1;//tianman
                break;
            case 0x09:
                //arm_count[ARM_INSTRUCTION_TEQ]+=1;//tianman
                break;
            case 0x0a:
                //arm_count[ARM_INSTRUCTION_CMP]+=1;//tianman
                break;
            case 0x0b:
                //arm_count[ARM_INSTRUCTION_CMN]+=1;//tianman
                break;
            case 0x0c:
                //arm_count[ARM_INSTRUCTION_ORR]+=1;//tianman
                break;
            case 0x0d:
                //arm_count[ARM_INSTRUCTION_MOV]+=1;//tianman
                break;
            case 0x0e:
                //arm_count[ARM_INSTRUCTION_BIC]+=1;//tianman
                break;
            default:
            case 0x0f:
                //arm_count[ARM_INSTRUCTION_MVN]+=1;//tianman
                break;
        }
    } else {
        /* other instructions */
        op1 = (insn >> 24) & 0xf;
        switch(op1) {
            case 0x0:
            case 0x1:
                /* multiplies, extra load/stores */
                sh = (insn >> 5) & 3;
                if (sh == 0) {
                    if (op1 == 0x0) {
                        op1 = (insn >> 20) & 0xf;
                        switch (op1) {
                            case 0: case 1: case 2: case 3: case 6:
                                /* 32 bit mul */
                                instr_index = ARM_INSTRUCTION_MUL;//tianman
                                if (insn & (1 << 22)) {
                                    /* Subtract (mls) */
                                } else if (insn & (1 << 21)) {
                                    /* Add */
                                    instr_index = ARM_INSTRUCTION_MLA;//tianman
                                }
                                if (insn & (1 << 20)){
                                    instr_index++;//tianman
                                }
                                //arm_count[instr_index]+=1;//tianman
                                break;
                            default:
                                /* 64 bit mul */
                                if (insn & (1 << 22)){
                                    instr_index = ARM_INSTRUCTION_SMULL;//tianman
                                }
                                else{
                                    instr_index = ARM_INSTRUCTION_UMULL;//tianman
                                }
                                if (insn & (1 << 21)){ /* mult accumulate */
                                    if (insn & (1 << 22)){
                                        instr_index = ARM_INSTRUCTION_SMLAL;//tianman
                                    }
                                    else{
                                        instr_index = ARM_INSTRUCTION_UMLAL;//tianman
                                    }
                                }
                                if (!(insn & (1 << 23))) { /* double accumulate */
                                }
                                if (insn & (1 << 20)){
                                    instr_index++;
                                }
                                //arm_count[instr_index]+=1;//tianman
                                break;
                        }
                    } else {
                        if (insn & (1 << 23)) {
                            /* load/store exclusive */
                            op1 = (insn >> 21) & 0x3;
                            if (insn & (1 << 20)) {
                                switch (op1) {
                                    //arm_count[ARM_INSTRUCTION_LDREX]+=1;//tianman
                                    case 0: /* ldrex */
                                    break;
                                    case 1: /* ldrexd */
                                    break;
                                    case 2: /* ldrexb */
                                    break;
                                    case 3: /* ldrexh */
                                    break;
                                    default:
                                    abort();
                                }
                            } else {
                                //arm_count[ARM_INSTRUCTION_STREX]+=1;//tianman
                                switch (op1) {
                                    case 0:  /*  strex */
                                        break;
                                    case 1: /*  strexd */
                                        break;
                                    case 2: /*  strexb */
                                        break;
                                    case 3: /* strexh */
                                        break;
                                    default:
                                        abort();
                                }
                            }
                        } else {
                            /* SWP instruction */

                            /* ??? This is not really atomic.  However we know
                               we never have multiple CPUs running in parallel,
                               so it is good enough.  */
                            if (insn & (1 << 22)) {
                                //arm_count[ARM_INSTRUCTION_SWPB]+=1;//tianman
                            } else {
                                //arm_count[ARM_INSTRUCTION_SWP]+=1;//tianman
                            }
                        }
                    }
                } else {
                    /* Misc load/store */
                    if (insn & (1 << 24)){
                        /* Misc load/store */
                    }
                    if (insn & (1 << 20)) {
                        /* load */
                        switch(sh) {
                            case 1:
                                //arm_count[ARM_INSTRUCTION_LDRH]+=1;//tianman
                                break;
                            case 2:
                                //arm_count[ARM_INSTRUCTION_LDRSB]+=1;//tianman
                                break;
                            default:
                                //arm_count[ARM_INSTRUCTION_LDRSH]+=1;//tianman
                            case 3:
                                break;
                        }
                    } else if (sh & 2) {
                        /* doubleword */
                        if (sh & 1) {
                            /* store */
                            //arm_count[ARM_INSTRUCTION_STRD]+=1;//tianman
                        } else {
                            /* load */
                            //arm_count[ARM_INSTRUCTION_LDRD]+=1;//tianman
                        }
                    } else {
                        /* store */
                        //arm_count[ARM_INSTRUCTION_STRH]+=1;//tianman
                    }
                    /* Perform base writeback before the loaded value to
                       ensure correct behavior with overlapping index registers.
                       ldrd with base writeback is is undefined if the
                       destination and index registers overlap.  */
                    if (!(insn & (1 << 24))) {
                    } else if (insn & (1 << 21)) {
                    } else {
                    }
                }
                break;
            case 0x4:
            case 0x5:
                goto do_ldst_tianman;
            case 0x6:
            case 0x7:
                if (insn & (1 << 4)) {
                    /* Armv6 Media instructions.  */
                    switch ((insn >> 23) & 3) {
                        case 0: /* Parallel add/subtract.  */
                            break;
                        case 1:
                            if ((insn & 0x00700020) == 0) {
                                /* Halfword pack.  */
                                shift = (insn >> 7) & 0x1f;
                                if (insn & (1 << 6)) {
                                    /* pkhtb */
                                    //arm_count[ARM_INSTRUCTION_PKHTB]+=1;//tianman
                                    if (shift == 0)
                                        shift = 31;
                                } else {
                                    /* pkhbt */
                                    //arm_count[ARM_INSTRUCTION_PKHBT]+=1;//tianman
                                }
                            } else if ((insn & 0x00200020) == 0x00200000) {
                                /* [us]sat */
                                sh = (insn >> 16) & 0x1f;
                                if (sh != 0) {
                                    if (insn & (1 << 22)){
                                        //arm_count[ARM_INSTRUCTION_USAT]+=1;//tianman
                                    }
                                    else{
                                        //arm_count[ARM_INSTRUCTION_SSAT]+=1;//tianman
                                    }
                                }
                            } else if ((insn & 0x00300fe0) == 0x00200f20) {
                                /* [us]sat16 */
                                sh = (insn >> 16) & 0x1f;
                                if (sh != 0) {
                                    if (insn & (1 << 22)){
                                        //arm_count[ARM_INSTRUCTION_USAT16]+=1;//tianman
                                    }
                                    else{
                                        //arm_count[ARM_INSTRUCTION_SSAT16]+=1;//tianman
                                    }
                                }
                            } else if ((insn & 0x00700fe0) == 0x00000fa0) {
                                /* Select bytes.  */
                                //arm_count[ARM_INSTRUCTION_SEL]+=1;//tianman
                            } else if ((insn & 0x000003e0) == 0x00000060) {
                                //shift = (insn >> 10) & 3;
                                /* ??? In many cases it's not neccessary to do a
                                   rotate, a shift is sufficient.  */
                                //                        if (shift != 0)
                                //                            tcg_gen_rotri_i32(tmp, tmp, shift * 8);
                                op1 = (insn >> 20) & 7;
                                switch (op1) {
                                    case 0:
                                        instr_index = ARM_INSTRUCTION_SXTB16;//tianman
                                        break;
                                    case 2:
                                        instr_index = ARM_INSTRUCTION_SXTB;//tianman
                                        break;
                                    case 3:
                                        instr_index = ARM_INSTRUCTION_SXTH;//tianman
                                        break;
                                    case 4:
                                        instr_index = ARM_INSTRUCTION_UXTB16;//tianman
                                        break;
                                    case 6:
                                        instr_index = ARM_INSTRUCTION_UXTB;//tianman
                                        break;
                                    case 7:
                                        instr_index = ARM_INSTRUCTION_UXTH;//tianman
                                        break;
                                    default: goto illegal_op;
                                }
                                if (rn != 15) {
                                    instr_index -= 3;//tianman
                                }
                                //arm_count[instr_index]+=1;//tianman
                            } else if ((insn & 0x003f0f60) == 0x003f0f20) {
                                /* rev */
                                if (insn & (1 << 22)) {
                                    if (insn & (1 << 7)) {
                                    } else {
                                    }
                                } else {
                                    if (insn & (1 << 7)){
                                        //arm_count[ARM_INSTRUCTION_REV16]+=1;//tianman
                                    }
                                    else{
                                        //arm_count[ARM_INSTRUCTION_REV]+=1;//tianman
                                    }
                                }
                            } else {
                                goto illegal_op;
                            }
                            break;
                        case 2: /* Multiplies (Type 3).  */
                            if (insn & (1 << 20)) {
                                /* Signed multiply most significant [accumulate].  */
                                if (rd != 15) {
                                    if (insn & (1 << 6)) {
                                        //arm_count[ARM_INSTRUCTION_SMMLS]+=1;//tianman
                                    } else {
                                        //arm_count[ARM_INSTRUCTION_SMMLA]+=1;//tianman
                                    }
                                }
                                else{
                                    //arm_count[ARM_INSTRUCTION_SMMUL]+=1;//tianman
                                }
                            } else {
                                /* This addition cannot overflow.  */
                                if (insn & (1 << 6)) {
                                    instr_index = 1;//tianman
                                } else {
                                    instr_index = 0;//tianman
                                }
                                if (insn & (1 << 22)) {
                                    /* smlald, smlsld */
                                    instr_index += ARM_INSTRUCTION_SMLALD;//tianman
                                    //arm_count[instr_index]+=1;//tianman
                                } else {
                                    /* smuad, smusd, smlad, smlsd */
                                    if (rd != 15){
                                        instr_index += 2;//tianman
                                    }
                                    instr_index += ARM_INSTRUCTION_SMUAD;//tianman
                                    //arm_count[instr_index]+=1;//tianman
                                }
                            }
                            break;
                        case 3:
                            op1 = ((insn >> 17) & 0x38) | ((insn >> 5) & 7);
                            switch (op1) {
                                case 0: /* Unsigned sum of absolute differences.  */
                                    if (rd != 15) {
                                        //arm_count[ARM_INSTRUCTION_USADA8]+=1;//tianman
                                    }
                                    //arm_count[ARM_INSTRUCTION_USAD8]+=1;//tianman
                                    break;
                                case 0x20: case 0x24: case 0x28: case 0x2c:
                                    /* Bitfield insert/clear.  */
                                    break;
                                case 0x12: case 0x16: case 0x1a: case 0x1e: /* sbfx */
                                case 0x32: case 0x36: case 0x3a: case 0x3e: /* ubfx */
                                    break;
                                default:
                                    goto illegal_op;
                            }
                            break;
                    }
                    break;
                }
do_ldst_tianman:
                /* Check for undefined extension instructions
                 * per the ARM Bible IE:
                 * xxxx 0111 1111 xxxx  xxxx xxxx 1111 xxxx
                 */
                sh = (0xf << 20) | (0xf << 4);
                if (op1 == 0x7 && ((insn & sh) == sh))
                {
                    goto illegal_op;
                }
                /* load/store byte/word */
                rn = (insn >> 16) & 0xf;
                rd = (insn >> 12) & 0xf;
                //i = (IS_USER(s) || (insn & 0x01200000) == 0x00200000);
                if (insn & (1 << 20)) {
                    /* load */
                    if (insn & (1 << 22)) {
                        //arm_count[ARM_INSTRUCTION_LDRB]+=1;//tianman
                    } else {
                        //arm_count[ARM_INSTRUCTION_LDR]+=1;//tianman
                    }
                } else {
                    /* store */
                    if (insn & (1 << 22))
                    {
                        //arm_count[ARM_INSTRUCTION_STRB]+=1;//tianman
                    }
                    else
                    {
                        //arm_count[ARM_INSTRUCTION_STR]+=1;//tianman
                    }
                }
                //				if (insn & (1 << 20)) {
                //					/* Complete the load.  */
                //				}
                break;
            case 0x08:
            case 0x09:
                {
                    /* load/store multiple words */
                    /* XXX: store correct base if write back */
                    switch (insn & 0x00500000 >> 20) {//tianman
                        case 0x0:
                            //arm_count[ARM_INSTRUCTION_STM1]+=1;//tianman
                            break;
                        case 0x1:
                            //arm_count[ARM_INSTRUCTION_LDM1]+=1;//tianman
                            break;
                        case 0x4:
                            //arm_count[ARM_INSTRUCTION_STM2]+=1;//tianman
                            break;
                        case 0x5:
                            if (insn & (1 << 15))
                                //arm_count[ARM_INSTRUCTION_LDM3]+=1;//tianman
                            else
                                //arm_count[ARM_INSTRUCTION_LDM2]+=1;//tianman
                            break;
                    }
#if 0
                    user = 0;
                    if (insn & (1 << 22)) {
                        if (IS_USER(s))
                            goto illegal_op; /* only usable in supervisor mode */

                        if ((insn & (1 << 15)) == 0)
                            user = 1;
                    }
                    rn = (insn >> 16) & 0xf;
                    addr = load_reg(s, rn);

                    /* compute total size */
                    loaded_base = 0;
                    TCGV_UNUSED(loaded_var);
                    n = 0;
                    for(i=0;i<16;i++) {
                        if (insn & (1 << i))
                            n++;
                    }
                    /* XXX: test invalid n == 0 case ? */
                    if (insn & (1 << 23)) {
                        if (insn & (1 << 24)) {
                            /* pre increment */
                            tcg_gen_addi_i32(addr, addr, 4);
                        } else {
                            /* post increment */
                        }
                    } else {
                        if (insn & (1 << 24)) {
                            /* pre decrement */
                            tcg_gen_addi_i32(addr, addr, -(n * 4));
                        } else {
                            /* post decrement */
                            if (n != 1)
                                tcg_gen_addi_i32(addr, addr, -((n - 1) * 4));
                        }
                    }
                    if(insn & (1 << 20)) {
                        /* paslab : load instructions counter += 1 */
                        load_count += 1;
                    } else {
                        /* paslab : store instructions counter += 1 */
                        store_count += 1;
                    }
                    j = 0;
                    for(i=0;i<16;i++) {
                        if (insn & (1 << i)) {
                            if (insn & (1 << 20)) {
                                /* load */
                                if (i == 15) {
                                } else if (user) {
                                } else if (i == rn) {
                                } else {
                                }
                            } else {
                                /* store */
                                if (i == 15) {
                                    /* special case: r15 = PC + 8 */
                                } else if (user) {
                                } else {
                                }
                            }
                            j++;
                            /* no need to add after the last transfer */
                        }
                    }
                    if (insn & (1 << 21)) {
                        /* write back */
                        if (insn & (1 << 23)) {
                            if (insn & (1 << 24)) {
                                /* pre increment */
                            } else {
                                /* post increment */
                            }
                        } else {
                            if (insn & (1 << 24)) {
                                /* pre decrement */
                                if (n != 1)
                            } else {
                                /* post decrement */
                            }
                        }
                    } else {
                    }
                    if ((insn & (1 << 22)) && !user) {
                        /* Restore CPSR from SPSR.  */
                    }
#endif
                }
                break;
            case 0xa:
            case 0xb:
                {
                    /* paslab : support event 0x05 (B & BL count) */

                    /* branch (and link) */
                    if (insn & (1 << 24)) {
                        //arm_count[ARM_INSTRUCTION_B]+=1;//tianman
                    }
                    else
                        //arm_count[ARM_INSTRUCTION_B]+=1;//tianman

                }
                break;
            case 0xc:
            case 0xd:
            case 0xe:
                /* Coprocessor.  */
                //arm_count[ARM_INSTRUCTION_COPROCESSOR]+=1;//tianman
                break;
            case 0xf:
                /* swi */
                //arm_count[ARM_INSTRUCTION_SWI]+=1;//tianman
                break;
            default:
illegal_op:
                break;
        }
    }
}
#endif

/* Implement by evo0209
 * It's almost the same as analyze_arm_ticks, but return the insn latency.
 * It aims to calculate the ticks in each TB, instead of in helper function
 * TODO This might be wrong, need to check
 */
uint32_t CPU_CortexA9::Translation::_get_arm_ticks(uint32_t insn)
{

    unsigned int cond, op1, shift, rn, rd, sh;
    unsigned int instr_index = 0; // tianman

    cond = insn >> 28;
    if (cond == 0xf) {
        /* Unconditional instructions.  */
        if (((insn >> 25) & 7) == 1) {
            /* NEON Data processing.  */
            // arm_count[ARM_INSTRUCTION_NEON_DP]+=1;
            return arm_instr_time[ARM_INSTRUCTION_NEON_DP];
        }
        if ((insn & 0x0f100000) == 0x04000000) {
            /* NEON load/store.  */
            // arm_count[ARM_INSTRUCTION_NEON_LS]+=1;
            return arm_instr_time[ARM_INSTRUCTION_NEON_LS];
        }
        if ((insn & 0x0d70f000) == 0x0550f000) {
            // arm_count[ARM_INSTRUCTION_PLD]+=1;
            return arm_instr_time[ARM_INSTRUCTION_PLD]; /* PLD */
        } else if ((insn & 0x0ffffdff) == 0x01010000) {
            // arm_count[ARM_INSTRUCTION_SETEND]+=1;//tianman
            /* setend */
            return arm_instr_time[ARM_INSTRUCTION_SETEND];
        } else if ((insn & 0x0fffff00) == 0x057ff000) {
            switch ((insn >> 4) & 0xf) {
            case 1: /* clrex */
                // arm_count[ARM_INSTRUCTION_CLREX]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_CLREX];
            case 4: /* dsb */
                // arm_count[ARM_INSTRUCTION_DSB]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_DSB];
            case 5: /* dmb */
                // arm_count[ARM_INSTRUCTION_DMB]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_DMB];
            case 6: /* isb */
                // arm_count[ARM_INSTRUCTION_ISB]+=1;//tianman
                /* We don't emulate caches so these are a no-op.  */
                return arm_instr_time[ARM_INSTRUCTION_ISB];
            default:
                goto illegal_op;
            }
        } else if ((insn & 0x0e5fffe0) == 0x084d0500) {
            /* srs */
            // arm_count[ARM_INSTRUCTION_SRS]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_SRS];
        } else if ((insn & 0x0e5fffe0) == 0x081d0a00) {
            /* rfe */
            // arm_count[ARM_INSTRUCTION_RFE]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_RFE];
        } else if ((insn & 0x0e000000) == 0x0a000000) {
            /* branch link and change to thumb (blx <offset>) */
            // arm_count[ARM_INSTRUCTION_BLX]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_BLX];
        } else if ((insn & 0x0e000f00) == 0x0c000100) {
            // LDC,STC?
            if (insn & (1 << 20)) {
                // arm_count[ARM_INSTRUCTION_LDC]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_LDC];
            } else {
                // arm_count[ARM_INSTRUCTION_STC]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_STC];
            }
            // return;
        } else if ((insn & 0x0fe00000) == 0x0c400000) {
            /* Coprocessor double register transfer.  */
            // MCRR, MRRC
            if (insn & (1 << 20)) {
                // arm_count[ARM_INSTRUCTION_MRRC]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_MRRC];
            } else {
                // arm_count[ARM_INSTRUCTION_MCRR]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_MCRR];
            }
            // return;
        } else if ((insn & 0x0f000010) == 0x0e000010) {
            /* Additional coprocessor register transfer.  */
            // MCR,MRC
            if (insn & (1 << 20)) {
                // arm_count[ARM_INSTRUCTION_MRC]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_MRC];
            } else {
                // arm_count[ARM_INSTRUCTION_MCR]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_MCR];
            }
            // return;
        } else if ((insn & 0x0ff10020) == 0x01000000) {
            /* cps (privileged) */
            // arm_count[ARM_INSTRUCTION_CPS]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_CPS];
        }
        goto illegal_op;
    }

    if ((insn & 0x0f900000) == 0x03000000) {
        if ((insn & (1 << 21)) == 0) {
            if ((insn & (1 << 22)) == 0) {
                /* MOVW */
                // arm_count[ARM_INSTRUCTION_MOVW]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_MOVW];
            } else {
                /* MOVT */
                // arm_count[ARM_INSTRUCTION_MOVT]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_MOVT];
            }
        } else {
            if (((insn >> 16) & 0xf) == 0) {
            } else {
                /* CPSR = immediate */
                // arm_count[ARM_INSTRUCTION_MSR]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_MSR];
            }
        }
    } else if ((insn & 0x0f900000) == 0x01000000 && (insn & 0x00000090) != 0x00000090) {
        /* miscellaneous instructions */
        op1 = (insn >> 21) & 3;
        sh  = (insn >> 4) & 0xf;
        switch (sh) {
        case 0x0: /* move program status register */
            if (op1 & 1) {
                /* PSR = reg */
                // arm_count[ARM_INSTRUCTION_MSR]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_MSR];
            } else {
                /* reg = PSR */
                // arm_count[ARM_INSTRUCTION_MRS]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_MRS];
            }
            break;
        case 0x1:
            if (op1 == 1) {
                // arm_count[ARM_INSTRUCTION_BX]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_BX];
                /* branch/exchange thumb (bx).  */
            } else if (op1 == 3) {
                /* clz */
                // arm_count[ARM_INSTRUCTION_CLZ]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_CLZ];
            } else {
                goto illegal_op;
            }
            break;
        case 0x2:
            if (op1 == 1) {
                // arm_count[ARM_INSTRUCTION_BXJ]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_BXJ];
            } else {
                goto illegal_op;
            }
            break;
        case 0x3:
            if (op1 != 1) goto illegal_op;
            // arm_count[ARM_INSTRUCTION_BLX]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_BLX];

            /* branch link/exchange thumb (blx) */
            break;
        case 0x5: /* saturating add/subtract */
            if (op1 & 2) {
                if (op1 & 1) {
                    // arm_count[ARM_INSTRUCTION_QDSUB]+=1;//tianman
                    return arm_instr_time[ARM_INSTRUCTION_QDSUB];
                } else {
                    // arm_count[ARM_INSTRUCTION_QDADD]+=1;//tianman
                    return arm_instr_time[ARM_INSTRUCTION_QADD];
                }
            }
            if (op1 & 1) {
                // arm_count[ARM_INSTRUCTION_QSUB]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_QSUB];
            } else {
                // arm_count[ARM_INSTRUCTION_QADD]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_QADD];
            }
            break;
        case 7: /* bkpt */
            // arm_count[ARM_INSTRUCTION_BKPT]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_BKPT];
            break;
        case 0x8: /* signed multiply */
        case 0xa:
        case 0xc:
        case 0xe:
            if (op1 == 1) {
                /* (32 * 16) >> 16 */
                if ((sh & 2) == 0) {
                    // arm_count[ARM_INSTRUCTION_SMLAWY]+=1;//tianman
                    return arm_instr_time[ARM_INSTRUCTION_SMLAWY];
                }
                // arm_count[ARM_INSTRUCTION_SMULWY]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_SMULWY];
            } else {
                /* 16 * 16 */
                if (op1 == 3) {
                    // arm_count[ARM_INSTRUCTION_SMULXY]+=1;//tianman
                    return arm_instr_time[ARM_INSTRUCTION_SMULXY];
                } else if (op1 == 2) {
                    // arm_count[ARM_INSTRUCTION_SMLALXY]+=1;//tianman
                    return arm_instr_time[ARM_INSTRUCTION_SMLALXY];
                } else {
                    if (op1 == 0) {
                        // arm_count[ARM_INSTRUCTION_SMLAXY]+=1;//tianman
                        return arm_instr_time[ARM_INSTRUCTION_SMLAXY];
                    }
                }
            }
            break;
        default:
            goto illegal_op;
        }
    } else if (((insn & 0x0e000000) == 0 && (insn & 0x00000090) != 0x90)
               || ((insn & 0x0e000000) == (1 << 25))) {
#if 0
        int set_cc, logic_cc, shiftop;

        set_cc = (insn >> 20) & 1;
        logic_cc = table_logic_cc[op1] & set_cc;

        /* data processing instruction */
        if (insn & (1 << 25)) {
            /* immediate operand */
            val = insn & 0xff;
            shift = ((insn >> 8) & 0xf) * 2;
            if (shift) {
                val = (val >> shift) | (val << (32 - shift));
            }
            tmp2 = new_tmp();
            tcg_gen_movi_i32(tmp2, val);
            if (logic_cc && shift) {
                gen_set_CF_bit31(tmp2);
            }
        } else {
            /* register */
            rm = (insn) & 0xf;
            tmp2 = load_reg(s, rm);
            shiftop = (insn >> 5) & 3;
            if (!(insn & (1 << 4))) {
                shift = (insn >> 7) & 0x1f;
                gen_arm_shift_im(tmp2, shiftop, shift, logic_cc);
            } else {
                rs = (insn >> 8) & 0xf;
                tmp = load_reg(s, rs);
                gen_arm_shift_reg(tmp2, shiftop, tmp, logic_cc);
            }
        }
        if (op1 != 0x0f && op1 != 0x0d) {
            rn = (insn >> 16) & 0xf;
            tmp = load_reg(s, rn);
        } else {
            TCGV_UNUSED(tmp);
        }
#endif
        op1 = (insn >> 21) & 0xf;
        rd  = (insn >> 12) & 0xf;
        switch (op1) {
        case 0x00:
            // arm_count[ARM_INSTRUCTION_AND]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_AND];
            break;
        case 0x01:
            // arm_count[ARM_INSTRUCTION_EOR]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_EOR];
            break;
        case 0x02:
            // arm_count[ARM_INSTRUCTION_SUB]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_SUB];
            break;
        case 0x03:
            // arm_count[ARM_INSTRUCTION_RSB]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_RSB];
            break;
        case 0x04:
            // arm_count[ARM_INSTRUCTION_ADD]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_ADD];
            break;
        case 0x05:
            // arm_count[ARM_INSTRUCTION_ADC]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_ADC];
            break;
        case 0x06:
            // arm_count[ARM_INSTRUCTION_SBC]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_SBC];
            break;
        case 0x07:
            // arm_count[ARM_INSTRUCTION_RSC]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_RSC];
            break;
        case 0x08:
            // arm_count[ARM_INSTRUCTION_TST]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_TST];
            break;
        case 0x09:
            // arm_count[ARM_INSTRUCTION_TEQ]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_TEQ];
            break;
        case 0x0a:
            // arm_count[ARM_INSTRUCTION_CMP]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_CMP];
            break;
        case 0x0b:
            // arm_count[ARM_INSTRUCTION_CMN]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_CMN];
            break;
        case 0x0c:
            // arm_count[ARM_INSTRUCTION_ORR]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_ORR];
            break;
        case 0x0d:
            // arm_count[ARM_INSTRUCTION_MOV]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_MOV];
            break;
        case 0x0e:
            // arm_count[ARM_INSTRUCTION_BIC]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_BIC];
            break;
        default:
        case 0x0f:
            // arm_count[ARM_INSTRUCTION_MVN]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_MVN];
            break;
        }
    } else {
        /* other instructions */
        op1 = (insn >> 24) & 0xf;
        switch (op1) {
        case 0x0:
        case 0x1:
            /* multiplies, extra load/stores */
            sh = (insn >> 5) & 3;
            if (sh == 0) {
                if (op1 == 0x0) {
                    op1 = (insn >> 20) & 0xf;
                    switch (op1) {
                    case 0:
                    case 1:
                    case 2:
                    case 3:
                    case 6:
                        /* 32 bit mul */
                        instr_index = ARM_INSTRUCTION_MUL; // tianman
                        if (insn & (1 << 22)) {
                            /* Subtract (mls) */
                        } else if (insn & (1 << 21)) {
                            /* Add */
                            instr_index = ARM_INSTRUCTION_MLA; // tianman
                        }
                        if (insn & (1 << 20)) {
                            instr_index++; // tianman
                        }
                        // arm_count[instr_index]+=1;//tianman
                        break;
                    default:
                        /* 64 bit mul */
                        if (insn & (1 << 22)) {
                            instr_index = ARM_INSTRUCTION_SMULL; // tianman
                        } else {
                            instr_index = ARM_INSTRUCTION_UMULL; // tianman
                        }
                        if (insn & (1 << 21)) { /* mult accumulate */
                            if (insn & (1 << 22)) {
                                instr_index = ARM_INSTRUCTION_SMLAL; // tianman
                            } else {
                                instr_index = ARM_INSTRUCTION_UMLAL; // tianman
                            }
                        }
                        if (!(insn & (1 << 23))) { /* double accumulate */
                        }
                        if (insn & (1 << 20)) {
                            instr_index++;
                        }
                        // arm_count[instr_index]+=1;//tianman
                        return arm_instr_time[instr_index];
                        break;
                    }
                } else {
                    if (insn & (1 << 23)) {
                        /* load/store exclusive */
                        op1 = (insn >> 21) & 0x3;
                        if (insn & (1 << 20)) {
                            switch (op1) {
                            // arm_count[ARM_INSTRUCTION_LDREX]+=1;//tianman
                            case 0: /* ldrex */
                                break;
                            case 1: /* ldrexd */
                                break;
                            case 2: /* ldrexb */
                                break;
                            case 3: /* ldrexh */
                                break;
                            default:
                                abort();
                            }
                            return arm_instr_time[ARM_INSTRUCTION_LDREX];
                        } else {
                            // arm_count[ARM_INSTRUCTION_STREX]+=1;//tianman
                            switch (op1) {
                            case 0: /*  strex */
                                break;
                            case 1: /*  strexd */
                                break;
                            case 2: /*  strexb */
                                break;
                            case 3: /* strexh */
                                break;
                            default:
                                abort();
                            }
                            return arm_instr_time[ARM_INSTRUCTION_STREX];
                        }
                    } else {
                        /* SWP instruction */

                        /* ??? This is not really atomic.  However we know
                           we never have multiple CPUs running in parallel,
                           so it is good enough.  */
                        if (insn & (1 << 22)) {
                            // arm_count[ARM_INSTRUCTION_SWPB]+=1;//tianman
                            return arm_instr_time[ARM_INSTRUCTION_SWPB];
                        } else {
                            // arm_count[ARM_INSTRUCTION_SWP]+=1;//tianman
                            return arm_instr_time[ARM_INSTRUCTION_SWP];
                        }
                    }
                }
            } else {
                /* Misc load/store */
                if (insn & (1 << 24)) {
                    /* Misc load/store */
                }
                if (insn & (1 << 20)) {
                    /* load */
                    switch (sh) {
                    case 1:
                        // arm_count[ARM_INSTRUCTION_LDRH]+=1;//tianman
                        return arm_instr_time[ARM_INSTRUCTION_LDRH];
                        break;
                    case 2:
                        // arm_count[ARM_INSTRUCTION_LDRSB]+=1;//tianman
                        return arm_instr_time[ARM_INSTRUCTION_LDRSB];
                        break;
                    default:
                        // arm_count[ARM_INSTRUCTION_LDRSH]+=1;//tianman
                        return arm_instr_time[ARM_INSTRUCTION_LDRSH];
                    case 3:
                        break;
                    }
                } else if (sh & 2) {
                    /* doubleword */
                    if (sh & 1) {
                        /* store */
                        // arm_count[ARM_INSTRUCTION_STRD]+=1;//tianman
                        return arm_instr_time[ARM_INSTRUCTION_STRD];
                    } else {
                        /* load */
                        // arm_count[ARM_INSTRUCTION_LDRD]+=1;//tianman
                        return arm_instr_time[ARM_INSTRUCTION_LDRD];
                    }
                } else {
                    /* store */
                    // arm_count[ARM_INSTRUCTION_STRH]+=1;//tianman
                    return arm_instr_time[ARM_INSTRUCTION_STRH];
                }
                /* Perform base writeback before the loaded value to
                   ensure correct behavior with overlapping index registers.
                   ldrd with base writeback is is undefined if the
                   destination and index registers overlap.  */
                if (!(insn & (1 << 24))) {
                } else if (insn & (1 << 21)) {
                } else {
                }
            }
            break;
        case 0x4:
        case 0x5:
            goto do_ldst_tianman;
        case 0x6:
        case 0x7:
            if (insn & (1 << 4)) {
                /* Armv6 Media instructions.  */
                switch ((insn >> 23) & 3) {
                case 0: /* Parallel add/subtract.  */
                    break;
                case 1:
                    if ((insn & 0x00700020) == 0) {
                        /* Halfword pack.  */
                        shift = (insn >> 7) & 0x1f;
                        if (insn & (1 << 6)) {
                            /* pkhtb */
                            // arm_count[ARM_INSTRUCTION_PKHTB]+=1;//tianman
                            if (shift == 0) shift = 31;
                            return arm_instr_time[ARM_INSTRUCTION_PKHTB];
                        } else {
                            /* pkhbt */
                            // arm_count[ARM_INSTRUCTION_PKHBT]+=1;//tianman
                            return arm_instr_time[ARM_INSTRUCTION_PKHBT];
                        }
                    } else if ((insn & 0x00200020) == 0x00200000) {
                        /* [us]sat */
                        sh = (insn >> 16) & 0x1f;
                        if (sh != 0) {
                            if (insn & (1 << 22)) {
                                // arm_count[ARM_INSTRUCTION_USAT]+=1;//tianman
                                return arm_instr_time[ARM_INSTRUCTION_USAT];
                            } else {
                                // arm_count[ARM_INSTRUCTION_SSAT]+=1;//tianman
                                return arm_instr_time[ARM_INSTRUCTION_SSAT];
                            }
                        }
                    } else if ((insn & 0x00300fe0) == 0x00200f20) {
                        /* [us]sat16 */
                        sh = (insn >> 16) & 0x1f;
                        if (sh != 0) {
                            if (insn & (1 << 22)) {
                                // arm_count[ARM_INSTRUCTION_USAT16]+=1;//tianman
                                return arm_instr_time[ARM_INSTRUCTION_USAT16];
                            } else {
                                // arm_count[ARM_INSTRUCTION_SSAT16]+=1;//tianman
                                return arm_instr_time[ARM_INSTRUCTION_SSAT16];
                            }
                        }
                    } else if ((insn & 0x00700fe0) == 0x00000fa0) {
                        /* Select bytes.  */
                        // arm_count[ARM_INSTRUCTION_SEL]+=1;//tianman
                        return arm_instr_time[ARM_INSTRUCTION_SEL];
                    } else if ((insn & 0x000003e0) == 0x00000060) {
                        // shift = (insn >> 10) & 3;
                        /* ??? In many cases it's not neccessary to do a
                           rotate, a shift is sufficient.  */
                        //                        if (shift != 0)
                        //                            tcg_gen_rotri_i32(tmp, tmp, shift *
                        //                            8);
                        op1 = (insn >> 20) & 7;
                        rn  = (insn >> 16) & 0xf;
                        switch (op1) {
                        case 0:
                            instr_index = ARM_INSTRUCTION_SXTB16; // tianman
                            break;
                        case 2:
                            instr_index = ARM_INSTRUCTION_SXTB; // tianman
                            break;
                        case 3:
                            instr_index = ARM_INSTRUCTION_SXTH; // tianman
                            break;
                        case 4:
                            instr_index = ARM_INSTRUCTION_UXTB16; // tianman
                            break;
                        case 6:
                            instr_index = ARM_INSTRUCTION_UXTB; // tianman
                            break;
                        case 7:
                            instr_index = ARM_INSTRUCTION_UXTH; // tianman
                            break;
                        default:
                            goto illegal_op;
                        }
                        if (rn != 15) {
                            instr_index -= 3; // tianman
                        }
                        // arm_count[instr_index]+=1;//tianman
                        return arm_instr_time[instr_index];
                    } else if ((insn & 0x003f0f60) == 0x003f0f20) {
                        /* rev */
                        if (insn & (1 << 22)) {
                            if (insn & (1 << 7)) {
                            } else {
                            }
                        } else {
                            if (insn & (1 << 7)) {
                                // arm_count[ARM_INSTRUCTION_REV16]+=1;//tianman
                                return arm_instr_time[ARM_INSTRUCTION_REV16];
                            } else {
                                // arm_count[ARM_INSTRUCTION_REV]+=1;//tianman
                                return arm_instr_time[ARM_INSTRUCTION_REV];
                            }
                        }
                    } else {
                        goto illegal_op;
                    }
                    break;
                case 2: /* Multiplies (Type 3).  */
                    if (insn & (1 << 20)) {
                        /* Signed multiply most significant [accumulate].  */
                        rd = (insn >> 12) & 0xf;
                        if (rd != 15) {
                            if (insn & (1 << 6)) {
                                // arm_count[ARM_INSTRUCTION_SMMLS]+=1;//tianman
                                return arm_instr_time[ARM_INSTRUCTION_SMMLS];
                            } else {
                                // arm_count[ARM_INSTRUCTION_SMMLA]+=1;//tianman
                                return arm_instr_time[ARM_INSTRUCTION_SMMLA];
                            }
                        } else {
                            // arm_count[ARM_INSTRUCTION_SMMUL]+=1;//tianman
                            return arm_instr_time[ARM_INSTRUCTION_SMMUL];
                        }
                    } else {
                        /* This addition cannot overflow.  */
                        if (insn & (1 << 6)) {
                            instr_index = 1; // tianman
                        } else {
                            instr_index = 0; // tianman
                        }
                        if (insn & (1 << 22)) {
                            /* smlald, smlsld */
                            instr_index += ARM_INSTRUCTION_SMLALD; // tianman
                            // arm_count[instr_index]+=1;//tianman
                            return arm_instr_time[instr_index];
                        } else {
                            /* smuad, smusd, smlad, smlsd */
                            rd = (insn >> 12) & 0xf;
                            if (rd != 15) {
                                instr_index += 2; // tianman
                            }
                            instr_index += ARM_INSTRUCTION_SMUAD; // tianman
                            // arm_count[instr_index]+=1;//tianman
                            return arm_instr_time[instr_index];
                        }
                    }
                    break;
                case 3:
                    op1 = ((insn >> 17) & 0x38) | ((insn >> 5) & 7);
                    switch (op1) {
                    case 0: /* Unsigned sum of absolute differences.  */
                        rd = (insn >> 12) & 0xf;
                        if (rd != 15) {
                            // arm_count[ARM_INSTRUCTION_USADA8]+=1;//tianman
                            return arm_instr_time[ARM_INSTRUCTION_USADA8];
                        }
                        // arm_count[ARM_INSTRUCTION_USAD8]+=1;//tianman
                        return arm_instr_time[ARM_INSTRUCTION_USAD8];
                        break;
                    case 0x20:
                    case 0x24:
                    case 0x28:
                    case 0x2c:
                        /* Bitfield insert/clear.  */
                        break;
                    case 0x12:
                    case 0x16:
                    case 0x1a:
                    case 0x1e: /* sbfx */
                    case 0x32:
                    case 0x36:
                    case 0x3a:
                    case 0x3e: /* ubfx */
                        break;
                    default:
                        goto illegal_op;
                    }
                    break;
                }
                break;
            }
        do_ldst_tianman:
            /* Check for undefined extension instructions
             * per the ARM Bible IE:
             * xxxx 0111 1111 xxxx  xxxx xxxx 1111 xxxx
             */
            sh = (0xf << 20) | (0xf << 4);
            if (op1 == 0x7 && ((insn & sh) == sh)) {
                goto illegal_op;
            }
            /* load/store byte/word */
            rn = (insn >> 16) & 0xf;
            rd = (insn >> 12) & 0xf;
            // i = (IS_USER(s) || (insn & 0x01200000) == 0x00200000);
            if (insn & (1 << 20)) {
                /* load */
                if (insn & (1 << 22)) {
                    // arm_count[ARM_INSTRUCTION_LDRB]+=1;//tianman
                    return arm_instr_time[ARM_INSTRUCTION_LDRB];
                } else {
                    // arm_count[ARM_INSTRUCTION_LDR]+=1;//tianman
                    return arm_instr_time[ARM_INSTRUCTION_LDR];
                }
            } else {
                /* store */
                if (insn & (1 << 22)) {
                    // arm_count[ARM_INSTRUCTION_STRB]+=1;//tianman
                    return arm_instr_time[ARM_INSTRUCTION_STRB];
                } else {
                    // arm_count[ARM_INSTRUCTION_STR]+=1;//tianman
                    return arm_instr_time[ARM_INSTRUCTION_STR];
                }
            }
            //				if (insn & (1 << 20)) {
            //					/* Complete the load.  */
            //				}
            break;
        case 0x08:
        case 0x09: {
            /* load/store multiple words */
            /* XXX: store correct base if write back */
            switch (insn & 0x00500000 >> 20) { // tianman
            case 0x0:
                // arm_count[ARM_INSTRUCTION_STM1]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_STM1];
                break;
            case 0x1:
                // arm_count[ARM_INSTRUCTION_LDM1]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_LDM1];
                break;
            case 0x4:
                // arm_count[ARM_INSTRUCTION_STM2]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_STM2];
                break;
            case 0x5:
                if (insn & (1 << 15)) {
                    // arm_count[ARM_INSTRUCTION_LDM3]+=1;//tianman
                    return arm_instr_time[ARM_INSTRUCTION_LDM3];
                } else {
                    // arm_count[ARM_INSTRUCTION_LDM2]+=1;//tianman
                    return arm_instr_time[ARM_INSTRUCTION_LDM2];
                }
                break;
            }
#if 0
                    user = 0;
                    if (insn & (1 << 22)) {
                        if (IS_USER(s))
                            goto illegal_op; /* only usable in supervisor mode */

                        if ((insn & (1 << 15)) == 0)
                            user = 1;
                    }
                    rn = (insn >> 16) & 0xf;
                    addr = load_reg(s, rn);

                    /* compute total size */
                    loaded_base = 0;
                    TCGV_UNUSED(loaded_var);
                    n = 0;
                    for(i=0;i<16;i++) {
                        if (insn & (1 << i))
                            n++;
                    }
                    /* XXX: test invalid n == 0 case ? */
                    if (insn & (1 << 23)) {
                        if (insn & (1 << 24)) {
                            /* pre increment */
                            tcg_gen_addi_i32(addr, addr, 4);
                        } else {
                            /* post increment */
                        }
                    } else {
                        if (insn & (1 << 24)) {
                            /* pre decrement */
                            tcg_gen_addi_i32(addr, addr, -(n * 4));
                        } else {
                            /* post decrement */
                            if (n != 1)
                                tcg_gen_addi_i32(addr, addr, -((n - 1) * 4));
                        }
                    }
                    if(insn & (1 << 20)) {
                        /* paslab : load instructions counter += 1 */
                        load_count += 1;
                    } else {
                        /* paslab : store instructions counter += 1 */
                        store_count += 1;
                    }
                    j = 0;
                    for(i=0;i<16;i++) {
                        if (insn & (1 << i)) {
                            if (insn & (1 << 20)) {
                                /* load */
                                if (i == 15) {
                                } else if (user) {
                                } else if (i == rn) {
                                } else {
                                }
                            } else {
                                /* store */
                                if (i == 15) {
                                    /* special case: r15 = PC + 8 */
                                } else if (user) {
                                } else {
                                }
                            }
                            j++;
                            /* no need to add after the last transfer */
                        }
                    }
                    if (insn & (1 << 21)) {
                        /* write back */
                        if (insn & (1 << 23)) {
                            if (insn & (1 << 24)) {
                                /* pre increment */
                            } else {
                                /* post increment */
                            }
                        } else {
                            if (insn & (1 << 24)) {
                                /* pre decrement */
                                if (n != 1)
                            } else {
                                /* post decrement */
                            }
                        }
                    } else {
                    }
                    if ((insn & (1 << 22)) && !user) {
                        /* Restore CPSR from SPSR.  */
                    }
#endif
        } break;
        case 0xa:
        case 0xb: {
            /* paslab : support event 0x05 (B & BL count) */

            /* branch (and link) */
            if (insn & (1 << 24)) {
                // arm_count[ARM_INSTRUCTION_B]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_B];
            } else {
                // arm_count[ARM_INSTRUCTION_B]+=1;//tianman
                return arm_instr_time[ARM_INSTRUCTION_B];
            }

        } break;
        case 0xc:
        case 0xd:
        case 0xe:
            /* Coprocessor.  */
            // arm_count[ARM_INSTRUCTION_COPROCESSOR]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_COPROCESSOR];
            break;
        case 0xf:
            /* swi */
            // arm_count[ARM_INSTRUCTION_SWI]+=1;//tianman
            return arm_instr_time[ARM_INSTRUCTION_SWI];
            break;
        default:
        illegal_op:
            /*Christine ADD*/
            // Attemp to know the number of missing instructions.
            // arm_count[ARM_INSTRUCTION_UNKNOWN]+=1;
            ERR_MSG("Unknown instruction");

            break;
        }
    }
    // if can't disdinguish the instruction then return 1
    return 1;
}

/* Implement by evo0209
 * Check dual issue possibility, and reduce redundant ticks caculate
 * by analyze_arm_ticks if the two instruction could be dual issued
 */
uint16_t CPU_CortexA9::Translation::_dual_issue_check()
{
    /*Christine: This dual-issue pipeline is for Cortex-A7*/
    // It is a little bit different from Cortex-A9.
    // We don not include the whole situations, because the manual does not give us
    // complete details.
    uint64_t tmp_insn;
    int      op1;
    int      is_ls[2];
    int      is_imme[2];
    int      is_arith[2];
    int      is_dw[2]; // Christine: doubleword
    int      Rn[2];
    int      Rm[2];
    int      Rd[2];
    // int Rs[2] = {0};
    // int Rt[2] = {0};

    int i;
    for (i = 0; i < 2; i++) {
        tmp_insn = insn_buf[i];
        if ((tmp_insn & 0x0f900000) == 0x03000000) {
            Rd[i] = (tmp_insn >> 12) & 0xf;
            Rn[i] = (tmp_insn >> 16) & 0xf;
            if ((tmp_insn & (1 << 21)) != 0) {
                if ((((tmp_insn >> 12) & 0xf) == 0xf)
                    && (((tmp_insn >> 16) & 0xf) == 0)) {
                    is_imme[i] = 1; // immediate
                }
            }
        } else if ((tmp_insn & 0x0f900000) == 0x01000000
                   && (tmp_insn & 0x00000090) != 0x00000090) {
            /* miscellaneous instructions */
            ;
        } else if (((tmp_insn & 0x0e000000) == 0 && (tmp_insn & 0x00000090) != 0x90)
                   || ((tmp_insn & 0x0e000000) == (1 << 25))) {
            Rd[i] = (tmp_insn >> 12) & 0xf;
            Rn[i] = (tmp_insn >> 16) & 0xf;
            Rm[i] = tmp_insn & 0xf;
            if (tmp_insn & (1 << 25))
                is_imme[i] = 1; // immediate
            else {
                is_arith[i] = 1; // alu
            }
        } else {
            /* other instructions */
            op1    = (tmp_insn >> 24) & 0xf;
            int sh = (tmp_insn >> 5) & 3;
            Rd[i]  = (tmp_insn >> 12) & 0xf;
            Rn[i]  = (tmp_insn >> 16) & 0xf;
            if (sh & 2) {
                is_dw[i] = 1; // for doubleword
            }

            if (op1 == 0x4 || op1 == 0x5) is_ls[i] = 1; // load / store
        }
    }

    if (is_arith[0] == 1 && is_arith[1] == 1) {
        if (Rd[0] != Rd[1] && Rd[0] != Rm[1] && Rd[0] != Rn[1] && Rd[1] != Rm[0]
            && Rd[1] != Rn[0])
            return 1;
    } else if (is_arith[0] == 1 && is_ls[1] == 1) {
        if (Rd[0] != Rd[1] && Rd[0] != Rn[1] && Rm[0] != Rd[1] && Rn[0] != Rd[1])
            return 1;
    } else if (is_arith[1] == 1 && is_ls[0] == 1) {
        if (Rd[1] != Rd[0] && Rd[1] != Rn[0] && Rm[1] != Rd[0] && Rn[1] != Rd[0])
            return 1;
    } else if (is_arith[0] == 1 && is_imme[1] == 1) {
        if (Rd[0] != Rd[1] && Rd[0] != Rn[1] && Rm[0] != Rd[1] && Rn[0] != Rd[1])
            return 1;
    } else if (is_imme[0] && is_imme[1] == 1) {
        if (Rd[0] != Rd[1]) return 1;
    } else if (is_dw[0] == 1 || is_dw[1] == 1) {
        return 1;
    }

    return 0;
}

/* the following array is used to deal with def-use register interlocks, which we
 * can compute statically (ignoring conditions), very fortunately.
 *
 * the idea is that interlock_base contains the number of cycles "executed" from
 * the start of a basic block.基本上不用管interlock_base，因為沒用到 It is set to 0 in
 * trace_bb_start, and incremented
 * in each call to get_insn_ticks.
 *
 * interlocks[N] correspond to the value of interlock_base after which a register N
 * can be used by another operation, it is set each time an instruction writes to
 * the register in get_insn_ticks()
 */

void CPU_CortexA9::Translation::_interlock_def(int reg, int delay) // interlock加delay
{
    if (reg >= 0) interlocks[reg] = interlock_base + delay;
}

int CPU_CortexA9::Translation::_interlock_use(int reg) // lock和interlock_base的差異
{
    int delay = 0;

    if (reg >= 0) {
        delay                = interlocks[reg] - interlock_base;
        if (delay < 0) delay = 0;
    }
    return delay;
}

// Compute the number of cycles that this instruction will take,
// not including any I-cache or D-cache misses. This function
// is called for each instruction in a basic block when that
// block is being translated.
// VPMU CORE
int CPU_CortexA9::Translation::_get_insn_ticks(uint32_t insn)
{
    int result = 1; /* by default, use 1 cycle */

    /* See Chapter 12 of the ARM920T Reference Manual for details about clock cycles */

    /* first check for invalid condition codes */
    if ((insn >> 28) == 0xf) { // ARM DDI 0100I  ARM Architecture Reference Manual A3-41
                               // temp_note by ppb
        if ((insn >> 25) == 0x7d) { /* BLX */
            // result = 3;
            // evo0209
            result = arm_instr_time[ARM_INSTRUCTION_BLX];
            goto Exit;
        }
        // added by ppb
        else if ((insn & 0x0d70f000) == 0x0550f000) { /* PLD */
            // result = 1;
            // evo0209
            result = arm_instr_time[ARM_INSTRUCTION_PLD];
            goto Exit;
        }
        // end added by ppb
        /* XXX: if we get there, we're either in an UNDEFINED instruction */
        /* or in co-processor related ones. For now, only return 1 cycle */
        goto Exit;
    }

    /* other cases */
    switch ((insn >> 25) & 7) {
    case 0: // 25-27 bit = 0  temp_note by ppb
        if ((insn & 0x00000090)
            == 0x00000090) /* Multiplies, extra load/store, Table 3-2 */
        // extract out all (5-8 bit)= 1001  temp_note by  ppb
        {
            /* XXX: TODO: Add support for multiplier operand content penalties in the
             * translator */

            // haven't done : ARM DDI 0222B p.8-20 SMULxy, SMLAxy, SMULWy, SMLAWy  by ppb
            if ((insn & 0x0fc000f0) == 0x00000090) /* 3-2: Multiply (accumulate) */
            { // ARM DDI 0100I  Figure A3-3 in A3-35   temp_note by ppb

                int Rm = (insn & 15);
                int Rs = (insn >> 8) & 15;
                int Rn = (insn >> 12) & 15;
                int Rd = (insn >> 16) & 15; // added by ppb

                if ((insn & 0x00200000) != 0) { /* MLA */
                    // result += _interlock_use(Rn); // comment out by ppb
                    if (_interlock_use(Rn) > 1) { // added by ppb
                        result += _interlock_use(Rn) - 1;
                    }
                } else {         /* MLU */
                    if (Rn != 0) /* UNDEFINED */
                        goto Exit;
                }
                /* cycles=2+m, assume m=1, this should be adjusted at interpretation time
                 */
                // result += 2 + _interlock_use(Rm) + _interlock_use(Rs); // commented out
                // by ppb
                // add the following if-else  by ppb
                if ((insn & 0x00100000) != 0) { /* MULS or MLAS  need 4 cycles */
                    // result += 3 + _interlock_use(Rm) + _interlock_use(Rs);
                    // evo0209
                    result += arm_instr_time[ARM_INSTRUCTION_MULS] - 1
                              + _interlock_use(Rm) + _interlock_use(Rs);
                } else { // need 2 cycles
                    // result += 1 + _interlock_use(Rm) + _interlock_use(Rs);
                    // evo0209
                    result += arm_instr_time[ARM_INSTRUCTION_MUL] - 1 + _interlock_use(Rm)
                              + _interlock_use(Rs);
                }

                _interlock_def(Rd, result + 1); // added by ppb
            } else if ((insn & 0x0f8000f0)
                       == 0x00800090) /* 3-2: Multiply (accumulate) long */
            {
                int Rm   = (insn & 15);
                int Rs   = (insn >> 8) & 15;
                int RdLo = (insn >> 12) & 15;
                int RdHi = (insn >> 16) & 15;

                if ((insn & 0x00200000) != 0) { /* SMLAL & UMLAL */ // (accumulate)
                    // result += _interlock_use(RdLo) + _interlock_use(RdHi); //comment
                    // out by ppb
                    if (_interlock_use(RdLo) > 1) { // added by ppb
                        result += _interlock_use(RdLo) - 1;
                    }
                    if (_interlock_use(RdHi) > 1) { // added by ppb
                        result += _interlock_use(RdHi) - 1;
                    }
                }
                /* else SMLL and UMLL */

                /* cucles=3+m, assume m=1, this should be adjusted at interpretation time
                 */
                // result += 3 + _interlock_use(Rm) + _interlock_use(Rs); commented out by
                // ppb
                if ((insn & 0x00100000)
                    != 0) { /* SMULLS, UMULLS, SMLALS, UMLALS, need 5 cycles */
                    // result += 4 + _interlock_use(Rm) + _interlock_use(Rs);
                    // evo0209
                    result += arm_instr_time[ARM_INSTRUCTION_SMULLS] - 1
                              + _interlock_use(Rm) + _interlock_use(Rs);
                } else { // need 3 cycles
                    // result += 2 + _interlock_use(Rm) + _interlock_use(Rs);
                    // evo0209
                    result += arm_instr_time[ARM_INSTRUCTION_SMULL] - 1
                              + _interlock_use(Rm) + _interlock_use(Rs);
                }

                _interlock_def(RdLo, result + 1); // added by ppb
                _interlock_def(RdHi, result + 1); // added by ppb
            }
            // else if ((insn & 0x0fd00ff0) == 0x01000090) /* 3-2: Swap/swap byte */ //by
            // ppb
            else if ((insn & 0x0fb00ff0) == 0x01000090) /* 3-2: Swap/swap byte */
            { // ARM DDI 0100I  Figure A3-5 in A3-39   temp_note by ppb
                int Rm = (insn & 15);
                int Rd = (insn >> 8) & 15;

                result =
                  2 + _interlock_use(Rm); // 2cycle是基本盤，如果load use的話需要3cycle
                //用_interlock_def來預防有人要接下來使用Rd(因為其他指令要用到Rd一定會call
                //_interlock_use(Rd)
                //來測試是否和上個指令衝突而需多算的cycle)
                //_interlock_def(Rd,
                // result+1);//rd這個lock=base(直到上個指令翻譯完總共花多少cycle)+(result+1)
                // result代表這個insn花了多少cycle
                // added by ppb
                if ((insn & 0x00400000) != 0) { // SWPB
                    //_interlock_def(Rd, result + 2);
                    // evo0209
                    _interlock_def(Rd, result + arm_instr_time[ARM_INSTRUCTION_SWPB] - 1);
                } else { // SWP
                    //_interlock_def(Rd, result + 1);
                    // evo0209
                    _interlock_def(Rd, result + arm_instr_time[ARM_INSTRUCTION_SWP] - 1);
                }
                // end added by ppb
            }
            // else if ((insn & 0x0e400ff0) == 0x00000090) /* 3-2: load/store halfword,
            // reg offset */ //by ppb
            else if ((insn & 0x0e400ff0)
                     == 0x000000b0) /* 3-2: load/store halfword, reg offset */
            {
                int Rm = (insn & 15);
                int Rd = (insn >> 12) & 15;
                int Rn = (insn >> 16) & 15;

                result += _interlock_use(Rn) + _interlock_use(Rm);
                if ((insn & 0x00100000)
                    != 0) /* it's a load, there's a 2-cycle interlock */
                    //_interlock_def(Rd, result + 2);
                    // evo0209
                    _interlock_def(Rd, result + arm_instr_time[ARM_INSTRUCTION_LDRH] - 1);
            }
            // else if ((insn & 0x0e400ff0) == 0x00400090) /* 3-2: load/store halfword,
            // imm offset */ //by ppb
            else if ((insn & 0x0e4000f0)
                     == 0x004000b0) /* 3-2: load/store halfword, imm offset */
            {
                int Rd = (insn >> 12) & 15;
                int Rn = (insn >> 16) & 15;

                result += _interlock_use(Rn);
                if ((insn & 0x00100000)
                    != 0) /* it's a load, there's a 2-cycle interlock */
                    //_interlock_def(Rd, result + 2);
                    // evo0209
                    _interlock_def(Rd, result + arm_instr_time[ARM_INSTRUCTION_LDRH] - 1);
            } else if ((insn & 0x0e500fd0)
                       == 0x000000d0) /* 3-2: load/store two words, reg offset */
            {
                /* XXX: TODO: Enhanced DSP instructions */
                // added by ppb
                int Rd = (insn >> 12) & 15;
                int Rn = (insn >> 16) & 15;
                result += 1 + _interlock_use(Rn); // 2 cycles
                if ((insn & 0x00000020) == 0)
                    if (Rd != 15)
                        //_interlock_def(Rd + 1, result + 1);
                        // evo0209
                        _interlock_def(Rd + 1,
                                       result + arm_instr_time[ARM_INSTRUCTION_LDR] - 1);
                // end added by ppb
            } else if ((insn & 0x0e500fd0)
                       == 0x001000d0) /* 3-2: load/store half/byte, reg offset */
            {                         // load signed half/byte reg offset
                int Rm = (insn & 15);
                int Rd = (insn >> 12) & 15;
                int Rn = (insn >> 16) & 15;

                result += _interlock_use(Rn) + _interlock_use(Rm);
                if ((insn & 0x00100000) != 0) /* load, 2-cycle interlock */
                    //_interlock_def(Rd, result + 2);
                    // evo0209
                    _interlock_def(Rd,
                                   result + arm_instr_time[ARM_INSTRUCTION_LDRSH] - 1);
            } else if ((insn & 0x0e5000d0)
                       == 0x004000d0) /* 3-2: load/store two words, imm offset */
            {
                /* XXX: TODO: Enhanced DSP instructions */
                // added by ppb
                int Rd = (insn >> 12) & 15;
                int Rn = (insn >> 16) & 15;
                result += 1 + _interlock_use(Rn); // 2 cycles
                if ((insn & 0x00000020) == 0)
                    if (Rd != 15)
                        //_interlock_def(Rd + 1, result+1);
                        // evo0209
                        _interlock_def(Rd + 1, arm_instr_time[ARM_INSTRUCTION_LDR] - 1);
                // end added by ppb
            } else if ((insn & 0x0e5000d0)
                       == 0x005000d0) /* 3-2: load/store half/byte, imm offset */
            {                         // load signed half/byte imm offset
                int Rd = (insn >> 12) & 15;
                int Rn = (insn >> 16) & 15;

                result += _interlock_use(Rn);
                if ((insn & 0x00100000) != 0) /* load, 2-cycle interlock */
                    //_interlock_def(Rd, result+2);
                    // evo0209
                    _interlock_def(Rd, arm_instr_time[ARM_INSTRUCTION_LDRSH] - 1);
            } else {
                /* UNDEFINED */
            }
        } else if ((insn & 0x0f900000) == 0x01000000) /* Misc. instructions, table 3-3 */
        // BX or Halfword data transfer  temp_note by ppb
        { // ARM DDI 0100I  Figure A3-4 in A3-37   temp_note by ppb
            switch ((insn >> 4) & 15) {
            case 0:
                if ((insn & 0x0fb0fff0)
                    == 0x0120f000) /* move register to status register MSR*/
                {
                    int Rm = (insn & 15);
                    // result += _interlock_use(Rm); // codes bolow added by ppb
                    // ppb
                    if (((insn >> 16) & 0x0007) > 0)
                        result += 2 + _interlock_use(Rm);
                    else
                        result +=
                          _interlock_use(Rm); // if only flags are updated(mask_f) by ppb
                    // end ppb
                } else if ((insn & 0xfbf0fff) == 0x010f0000) { // MRS  added by ppb
                    result += 1;                               // 2 cycles
                }
                break;

            case 1:
                // if ( ((insn & 0x0ffffff0) == 0x01200010) || /* branch/exchange */ //
                // comment out by ppb
                //     ((insn & 0x0fff0ff0) == 0x01600010) ) /* count leading zeroes */
                if ((insn & 0x0ffffff0) == 0x012fff10) /* branch/exchange */ // by ppb
                {
                    int Rm = (insn & 15); // in ARM9EJ-S TRM Chap.8, BX = 3 cycles  by ppb
                    // result += 2 + _interlock_use(Rm);
                    // evo0209
                    result += arm_instr_time[ARM_INSTRUCTION_BX] - 1 + _interlock_use(Rm);
                } else if ((insn & 0x0fff0ff0)
                           == 0x016f0f10) /* count leading zeroes CLZ  by ppb */
                {
                    int Rm = (insn & 15);
                    // result += _interlock_use(Rm);
                    // evo0209
                    result +=
                      arm_instr_time[ARM_INSTRUCTION_CLZ] - 1 + _interlock_use(Rm);
                }
                break;

            case 3:
                // if ((insn & 0x0ffffff0) == 0x01200030) /* link/exchange */ // comment
                // out by ppb
                if ((insn & 0x0ffffff0) == 0x012fff30) /* link/exchange */
                {
                    int Rm = (insn & 15);
                    // result += _interlock_use(Rm);
                    // result += 2 + _interlock_use(Rm); // BLX = 3 cycles by ppb
                    // evo0209
                    result +=
                      arm_instr_time[ARM_INSTRUCTION_BLX] - 1 + _interlock_use(Rm);
                }
                break;

            // other case: software breakpoint   temp_note by ppb
            default:
              /* TODO: Enhanced DSP instructions */
              ;
            }
        } else /* Data processing */
        {
            int Rm = (insn & 15);
            int Rn = (insn >> 16) & 15;

            result += _interlock_use(Rn) + _interlock_use(Rm);
            if ((insn & 0x10)) { /* register-controlled shift => 1 cycle penalty */
                int Rs = (insn >> 8) & 15;
                result += 1 + _interlock_use(Rs);
            }
            // added by ppb
            int Rd = (insn >> 12) & 15;
            if (Rd == 15) {
                result += 2;
            }
            // end added by ppb
        }
        break;

    case 1:
        // if ((insn & 0x01900000) == 0x01900000)
        if ((insn & 0x03b0f000) == 0x0320f000) {
            /* either UNDEFINED or move immediate to CPSR */
        } else /* Data processing immediate */
        {
            int Rn = (insn >> 12) & 15;
            result += _interlock_use(Rn);
        }
        break;

    case 2: /* load/store immediate */
    {
        int Rn = (insn >> 16) & 15;

        result += _interlock_use(Rn);
        if (insn & 0x00100000) { /* LDR */
            int Rd = (insn >> 12) & 15;

            if (Rd == 15) /* loading PC */
                // result = 5; // comment out by ppb
                result += 4;
            else
                _interlock_def(Rd, result + 1);
        }
    } break;

    case 3:
        if ((insn & 0x10) == 0) /* load/store register offset */
        {
            int Rm = (insn & 15);
            int Rn = (insn >> 16) & 15;

            result += _interlock_use(Rm) + _interlock_use(Rn);

            if (insn & 0x00100000) { /* LDR */
                int Rd = (insn >> 12) & 15;
                if (Rd == 15) {
                    if ((insn & 0xff0) == 0) // added by ppb
                        result = 5;
                    else            // scaled offset
                        result = 6; // added by ppb
                } else
                    _interlock_def(Rd, result + 1);
            } else {                     // store  added by ppb
                if ((insn & 0xff0) == 0) // added by ppb
                    result = 1;
                else            // scaled offset
                    result = 2; // added by ppb
            }
        }
        /* else Media inst.(ARMv6) or UNDEFINED */
        break;

    case 4: /* load/store multiple */
    {
        int      Rn   = (insn >> 16) & 15; // base regiester  temp_note by ppb
        uint32_t mask = (insn & 0xffff);
        int      count;

        for (count = 0; count < 15; count++) { // added by ppb
            if (mask & 1) {
                if (_interlock_use(count) == 1)
                    result += 2; // second-cycle interlock by ppb
                break;
            }
            mask = mask >> 1;
        }

        mask = (insn & 0xffff);
        for (count = 0; mask; count++) mask &= (mask - 1);

        result += _interlock_use(Rn);

        if (insn & 0x00100000) /* LDM */
        {
            int nn;

            if (insn & 0x8000) { /* loading PC */
                result = count + 4;
            } else { /* not loading PC */
                // result = (count < 2) ? 2 : count; //comment out by ppb
                result += ((count < 2) ? 2 : count) - 1;
            }
            /* create defs, all registers locked until the end of the load */
            for (nn = 0; nn < 15; nn++)
                if ((insn & (1U << nn)) != 0) _interlock_def(nn, result);
        } else /* STM */
            // result = (count < 2) ? 2 : count; //comment out by ppb
            result += ((count < 2) ? 2 : count) - 1;
    } break;

    case 5:         /* branch and branch+link */
        result = 3; // added by ppb
        break;

    case 6: /* coprocessor load/store */
    {
        int Rn = (insn >> 16) & 15;

        if (insn & 0x00100000) result += _interlock_use(Rn);

        /* XXX: other things to do ? */
    } break;

    default: /* i.e. 7 */
             /* XXX: TODO: co-processor related things */
             ;
    }
Exit:
    interlock_base += result;
    return result;
}

int CPU_CortexA9::Translation::_get_insn_ticks_thumb(uint32_t insn)
{
    // need implementation
    if (insn > 1000000)
        return 1;
    else
        return 0;
}

void CPU_CortexA9::build(VPMU_Inst& inst)
{
    log_debug("Initializing");

    log_debug(json_config.dump().c_str());

    auto model_name = vpmu::utils::get_json<std::string>(json_config, "name");
    strncpy(inst.model.name, model_name.c_str(), sizeof(inst.model.name));
    inst.model.frequency  = vpmu::utils::get_json<int>(json_config, "frequency");
    inst.model.dual_issue = vpmu::utils::get_json<bool>(json_config, "dual_issue");

    translator.build(json_config);
    log_debug("Initialized");
}

void CPU_CortexA9::packet_processor(int id, VPMU_Inst::Reference& ref, VPMU_Inst& inst)
{
#define CONSOLE_U64(str, val) CONSOLE_LOG(str " %'" PRIu64 "\n", (uint64_t)val)
#define CONSOLE_TME(str, val) CONSOLE_LOG(str " %'lf sec\n", (double)val / 1000000000.0)
#ifdef CONFIG_VPMU_DEBUG_MSG
    debug_packet_num_cnt++;
    if (ref.type == VPMU_PACKET_DUMP_INFO) {
        CONSOLE_LOG("    %'" PRIu64 " packets received\n", debug_packet_num_cnt);
        debug_packet_num_cnt = 0;
    }
#endif

    // Every simulators should handle VPMU_BARRIER_PACKET to support synchronization
    // The implementation depends on your own packet type and writing style
    switch (ref.type) {
    case VPMU_PACKET_BARRIER:
        inst.data.inst_cnt[0] = vpmu_total_inst_count(inst.data);
        inst.data.cycles[0]   = cycles[0];
        break;
    case VPMU_PACKET_DUMP_INFO:
        CONSOLE_LOG("  [%d] type : Cortex A9\n", id);
        CONSOLE_U64(" Total instruction count       :", vpmu_total_inst_count(inst.data));
        CONSOLE_U64("  ->User mode insn count       :", inst.data.user.total_inst);
        CONSOLE_U64("  ->Supervisor mode insn count :", inst.data.system.total_inst);
        CONSOLE_U64("  ->IRQ mode insn count        :", inst.data.interrupt.total_inst);
        CONSOLE_U64("  ->Other mode insn count      :", inst.data.rest.total_inst);
        CONSOLE_U64(" Total load instruction count  :", vpmu_total_load_count(inst.data));
        CONSOLE_U64("  ->User mode load count       :", inst.data.user.load);
        CONSOLE_U64("  ->Supervisor mode load count :", inst.data.system.load);
        CONSOLE_U64("  ->IRQ mode load count        :", inst.data.interrupt.load);
        CONSOLE_U64("  ->Other mode load count      :", inst.data.rest.load);
        CONSOLE_U64(" Total store instruction count :",
                    vpmu_total_store_count(inst.data));
        CONSOLE_U64("  ->User mode store count      :", inst.data.user.store);
        CONSOLE_U64("  ->Supervisor mode store count:", inst.data.system.store);
        CONSOLE_U64("  ->IRQ mode store count       :", inst.data.interrupt.store);
        CONSOLE_U64("  ->Other mode store count     :", inst.data.rest.store);

        break;
    case VPMU_PACKET_RESET:
        memset(cycles, 0, sizeof(cycles));
        memset(&inst.data, 0, sizeof(VPMU_Inst::Data));
        break;
    case VPMU_PACKET_DATA:
        accumulate(ref, inst.data);
        break;
    default:
        log_fatal("Unexpected packet");
    }

#undef CONSOLE_TME
#undef CONSOLE_U64
}

void CPU_CortexA9::accumulate(VPMU_Inst::Reference& ref, VPMU_Inst::Data& inst_data)
{
    VPMU_Inst::Inst_Data_Cell* cell = NULL;
    // Defining the types (struct) for communication
    enum CPU_MODE { // Copy from QEMU cpu.h
        USR = 0x10,
        FIQ = 0x11,
        IRQ = 0x12,
        SVC = 0x13,
        MON = 0x16,
        ABT = 0x17,
        HYP = 0x1a,
        UND = 0x1b,
        SYS = 0x1f
    };

    if (ref.mode == USR) {
        cell = &inst_data.user;
    } else if (ref.mode == SVC) {
        // if (ref.swi_fired_flag) { // TODO This feature is still lack of
        // setting this flag to true
        //    cell = &inst_data.system_call;
        //} else {
        //    cell = &inst_data.system;
        //}
        cell = &inst_data.system;
    } else if (ref.mode == IRQ) {
        cell = &inst_data.interrupt;
    } else {
        cell = &inst_data.rest;
    }
    cell->total_inst += ref.tb_counters_ptr->counters.total;
    cell->load += ref.tb_counters_ptr->counters.load;
    cell->store += ref.tb_counters_ptr->counters.store;
    cell->branch += ref.tb_counters_ptr->has_branch;
    cycles[ref.core] += ref.tb_counters_ptr->ticks;
}

#ifdef CONFIG_VPMU_VFP
void CPU_CortexA9::print_vfp_count(void)
{
#define etype(x) macro_str(x)
    int                i;
    uint64_t           counted                    = 0;
    uint64_t           total_counted              = 0;
    static const char* str_arm_vfp_instructions[] = {ARM_INSTRUCTION};

    for (i = 0; i < ARM_VFP_INSTRUCTION_TOTAL_COUNTS; i++) {
        if (GlobalVPMU.VFP_count[i] > 0) {
            CONSOLE_LOG(
              "%s: %llu ", str_arm_vfp_instructions[i], GlobalVPMU.VFP_count[i]);
            CONSOLE_LOG("need = %d spend cycle = %llu\n",
                        arm_vfp_instr_time[i],
                        GlobalVPMU.VFP_count[i] * arm_vfp_instr_time[i]);

            if (i < (ARM_VFP_INSTRUCTION_TOTAL_COUNTS - 2)) {
                counted += GlobalVPMU.VFP_count[i];
                total_counted += GlobalVPMU.VFP_count[i] * arm_vfp_instr_time[i];
            }
        }
    }
    // CONSOLE_LOG( "total latency: %llu\n", GlobalVPMU.VFP_BASE);
    // CONSOLE_LOG( "Counted instructions: %llu\n", counted);
    // CONSOLE_LOG( "total Counted cycle: %llu\n", total_counted);
    CONSOLE_LOG("VFP : total latency: %llu\n", GlobalVPMU.VFP_BASE);
    CONSOLE_LOG("VFP : Counted instructions: %llu\n", counted);
    CONSOLE_LOG("VFP : total Counted cycle: %llu\n", total_counted);
#undef etype
}
#endif
void CPU_CortexA9::Translation::build(nlohmann::json config)
{
    auto model_name = vpmu::utils::get_json<std::string>(config, "name");
    strncpy(cpu_model.name, model_name.c_str(), sizeof(cpu_model.name));
    cpu_model.frequency  = vpmu::utils::get_json<int>(config, "frequency");
    cpu_model.dual_issue = vpmu::utils::get_json<bool>(config, "dual_issue");

    nlohmann::json root = config["instruction"];
    // TODO move this to CPU model
    for (nlohmann::json::iterator it = root.begin(); it != root.end(); ++it) {
        // Skip the attribute next
        std::string key   = it.key();
        uint32_t    value = it.value();

        arm_instr_time[get_index_of_arm_inst(key.c_str())] = value;
    }
}

// TODO After removeing this from here to a separate module.
// And enable the timing feedback from VPMU to QEMU's virtual clock
// We should count the instruction count in order to make time move.
// And the final result of timing should subtract this value.
//====================  VPMU Translation Instrumentation   ===================
uint16_t CPU_CortexA9::Translation::get_arm_ticks(uint32_t insn)
{
    uint16_t ticks = 0;

    ticks = _get_arm_ticks(insn);
    // TODO FIXME
    if (cpu_model.dual_issue) {
        insn_buf[insn_buf_index] = insn;
        insn_buf_index++;
        if (insn_buf_index == 2) {
            ticks -= _dual_issue_check();
            insn_buf_index = 0;
        }
    }
    // DBG("%u\n", ticks);
    // ticks = get_insn_ticks(insn);
    // Remove unused function warnig
    (void)_get_insn_ticks(insn);
    return ticks;
}

uint16_t CPU_CortexA9::Translation::get_thumb_ticks(uint32_t insn)
{
    uint16_t ticks = 0;

    ticks = _get_insn_ticks_thumb(insn);
    return ticks;
    /* shocklink added to support branch count for portability */
    /* TODO: branch counter for thumb mode */
}

uint16_t CPU_CortexA9::Translation::get_cp14_ticks(uint32_t insn)
{
    // TODO This is still not implemented yet
    return 1;
}

#ifdef CONFIG_VPMU_VFP
uint16_t CPU_CortexA9::Translation::get_vfp_ticks(uint32_t insn, uint64_t vfp_vec_len)
{
    uint16_t ticks = 0;

    ticks = _analyze_vfp_ticks(insn, vfp_vec_len);
    // DBG("%u\n", get_arm_ticks(insn));
    // DBG("%u\n", ticks);
    // There is no dual-issue problem of VFP and NEON on Cortex-A7
    return ticks;
}
#endif
