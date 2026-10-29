//! Rafael Micro R820T / R820T2 / R828D tuner driver, a translation of `src/tuner_r82xx.c` of the RTL-SDR Blog fork of librtlsdr 1.3.6.
//!
//! Rafael Micro R820T/R828D driver, Copyright (C) 2013 Mauro Carvalho Chehab and Copyright (C) 2013 Steve Markgraf; a heavily modified version of the
//! Linux kernel driver (drivers/media/tuners/r820t.c); GPL-2.0-or-later. The RTL-SDR Blog changes (maximum VCO current, L-band PLL drop out, the V4 input
//! switching and notch filters) are included. Register values, tables and integer conversions are those of the C.

use crate::rtl::{RtlSdr, R820T_I2C_ADDR, R828D_I2C_ADDR};
use crate::transfer::UsbTransfer;

const REG_SHADOW_START: i32 = 5;
const NUM_REGS: usize = 30;
const VER_NUM: u8 = 49;

const HF: u8 = 1;
const VHF: u8 = 2;
const UHF: u8 = 3;

/// `enum r82xx_chip`
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub(crate) enum Chip {
    R820T,
    R828D,
}

/// `enum r82xx_xtal_cap_value`
#[derive(Clone, Copy, PartialEq, Eq)]
#[allow(dead_code)] // the unused variants document the other capacitor settings of the C (`r82xx_xtal_check` is not used by the driver)
enum XtalCap {
    Low30p,
    Low20p,
    Low10p,
    Low0p,
    High0p,
}

/// State of the tuner (`struct r82xx_config` and `struct r82xx_priv`).
pub(crate) struct R82 {
    pub(crate) i2c_addr: u8,
    pub(crate) xtal: u32,
    pub(crate) chip: Chip,
    max_i2c_msg_len: u32,
    use_predetect: bool,
    regs: [u8; NUM_REGS],
    xtal_cap_sel: XtalCap,
    int_freq: u32,
    rf_freq: u32,
    fil_cal_code: u8,
    input: u8,
    has_lock: bool,
    init_done: bool,
}

impl R82 {
    pub(crate) fn new() -> Self {
        R82 {
            i2c_addr: R820T_I2C_ADDR,
            xtal: 0,
            chip: Chip::R820T,
            max_i2c_msg_len: 8,
            use_predetect: false,
            regs: [0; NUM_REGS],
            xtal_cap_sel: XtalCap::Low30p,
            int_freq: 0,
            rf_freq: 0,
            fil_cal_code: 0,
            input: 0,
            has_lock: false,
            init_done: false,
        }
    }
}

/// Those initial values start from REG_SHADOW_START.
const INIT_ARRAY: [u8; NUM_REGS] = [
    0x83, 0x30, 0x75, 0xc0, 0x40, 0xd6, 0x6c, 0xf5, 0x63, 0x75, 0x68, 0x6c, 0x83, 0x80, 0x00, 0x0f, 0x00, 0xc0, 0x30, 0x48, 0xcc, 0x60,
    0x00, 0x54, 0xae, 0x4a, 0xc0, // 05 .. 1f (the C array has 27 initialisers, the rest zero)
    0x00, 0x00, 0x00,
];

/// Tuner frequency range (`struct r82xx_freq_range`): start in MHz, open_d, rf_mux_ploy, tf_c, xtal_cap20p, xtal_cap10p, xtal_cap0p.
struct Range(u32, u8, u8, u8, u8, u8, u8);

const FREQ_RANGES: [Range; 21] = [
    Range(0, 0x08, 0x02, 0xdf, 0x02, 0x01, 0x00),
    Range(50, 0x08, 0x02, 0xbe, 0x02, 0x01, 0x00),
    Range(55, 0x08, 0x02, 0x8b, 0x02, 0x01, 0x00),
    Range(60, 0x08, 0x02, 0x7b, 0x02, 0x01, 0x00),
    Range(65, 0x08, 0x02, 0x69, 0x02, 0x01, 0x00),
    Range(70, 0x08, 0x02, 0x58, 0x02, 0x01, 0x00),
    Range(75, 0x00, 0x02, 0x44, 0x02, 0x01, 0x00),
    Range(80, 0x00, 0x02, 0x44, 0x02, 0x01, 0x00),
    Range(90, 0x00, 0x02, 0x34, 0x01, 0x01, 0x00),
    Range(100, 0x00, 0x02, 0x34, 0x01, 0x01, 0x00),
    Range(110, 0x00, 0x02, 0x24, 0x01, 0x01, 0x00),
    Range(120, 0x00, 0x02, 0x24, 0x01, 0x01, 0x00),
    Range(140, 0x00, 0x02, 0x14, 0x01, 0x01, 0x00),
    Range(180, 0x00, 0x02, 0x13, 0x00, 0x00, 0x00),
    Range(220, 0x00, 0x02, 0x13, 0x00, 0x00, 0x00),
    Range(250, 0x00, 0x02, 0x11, 0x00, 0x00, 0x00),
    Range(280, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00),
    Range(310, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00),
    Range(450, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00),
    Range(588, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00),
    Range(650, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00),
];

const LNA_GAIN_STEPS: [i32; 16] = [0, 9, 13, 40, 38, 13, 31, 22, 26, 31, 26, 14, 19, 5, 35, 13];
const MIXER_GAIN_STEPS: [i32; 16] = [0, 5, 10, 10, 19, 9, 10, 25, 17, 10, 8, 16, 13, 6, 3, -8];
/// Bandwidth contribution by the low-pass filter.
const IF_LOW_PASS_BW_TABLE: [i32; 10] =
    [1_700_000, 1_600_000, 1_550_000, 1_450_000, 1_200_000, 900_000, 700_000, 550_000, 450_000, 350_000];
const FILT_HP_BW1: i32 = 350_000;
const FILT_HP_BW2: i32 = 380_000;

/// Bit reversal of a byte (the chip returns its registers bit reversed).
fn bitrev(byte: u8) -> u8 {
    const LUT: [u8; 16] = [0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe, 0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf];
    (LUT[(byte & 0xf) as usize] << 4) | LUT[(byte >> 4) as usize]
}

impl<T: UsbTransfer> RtlSdr<T> {
    /// Writes `val` at register `reg`, split into I2C messages of at most `max_i2c_msg_len - 1` bytes, and keeps the shadow copy (`r82xx_write`).
    fn r82_write(&mut self, reg: u8, val: &[u8]) -> i32 {
        // shadow_store
        let mut r = reg as i32 - REG_SHADOW_START;
        let mut slen = val.len() as i32;
        if r < 0 {
            slen += r;
            r = 0;
        }
        if slen > 0 {
            if slen > NUM_REGS as i32 - r {
                slen = NUM_REGS as i32 - r;
            }
            self.r82.regs[r as usize..(r + slen) as usize].copy_from_slice(&val[..slen as usize]);
        }
        let mut len = val.len();
        let mut reg = reg;
        let mut pos = 0usize;
        let max = (self.r82.max_i2c_msg_len - 1) as usize;
        loop {
            let size = len.min(max);
            let mut buf = [0u8; NUM_REGS + 1];
            buf[0] = reg;
            buf[1..=size].copy_from_slice(&val[pos..pos + size]);
            let addr = self.r82.i2c_addr;
            let rc = self.i2c_write(addr, &buf[..size + 1]);
            if rc != size as i32 + 1 {
                return if rc < 0 { rc } else { -1 };
            }
            reg = reg.wrapping_add(size as u8);
            len -= size;
            pos += size;
            if len == 0 {
                break;
            }
        }
        0
    }

    fn r82_write_reg(&mut self, reg: u8, val: u8) -> i32 {
        self.r82_write(reg, &[val])
    }

    fn r82_read_cache_reg(&self, reg: i32) -> i32 {
        let r = reg - REG_SHADOW_START;
        if (0..NUM_REGS as i32).contains(&r) {
            self.r82.regs[r as usize] as i32
        } else {
            -1
        }
    }

    fn r82_write_reg_mask(&mut self, reg: u8, val: u8, bit_mask: u8) -> i32 {
        let rc = self.r82_read_cache_reg(reg as i32);
        if rc < 0 {
            return rc;
        }
        let v = (rc as u8 & !bit_mask) | (val & bit_mask);
        self.r82_write(reg, &[v])
    }

    /// Reads `val.len()` registers from `reg` (write the register number, then read); the bytes are bit reversed back (`r82xx_read`).
    fn r82_read(&mut self, reg: u8, val: &mut [u8]) -> i32 {
        let addr = self.r82.i2c_addr;
        let rc = self.i2c_write(addr, &[reg]);
        if rc != 1 {
            return if rc < 0 { rc } else { -1 };
        }
        let len = val.len();
        let mut p = [0u8; NUM_REGS];
        let n = len.min(NUM_REGS);
        let rc = self.i2c_read(addr, &mut p[..n]);
        if rc != len as i32 {
            return if rc < 0 { rc } else { -1 };
        }
        for i in 0..n {
            val[i] = bitrev(p[i]);
        }
        0
    }

    fn r82_set_mux(&mut self, freq: u32) -> i32 {
        // Get the proper frequency range
        let freq = freq / 1_000_000;
        let mut i = 0;
        while i < FREQ_RANGES.len() - 1 {
            if freq < FREQ_RANGES[i + 1].0 {
                break;
            }
            i += 1;
        }
        let range = &FREQ_RANGES[i];
        // Open Drain
        let mut rc = self.r82_write_reg_mask(0x17, range.1, 0x08);
        if rc < 0 {
            return rc;
        }
        // RF_MUX,Polymux
        rc = self.r82_write_reg_mask(0x1a, range.2, 0xc3);
        if rc < 0 {
            return rc;
        }
        // TF BAND
        rc = self.r82_write_reg(0x1b, range.3);
        if rc < 0 {
            return rc;
        }
        // XTAL CAP & Drive
        let val = match self.r82.xtal_cap_sel {
            XtalCap::Low30p | XtalCap::Low20p => range.4 | 0x08,
            XtalCap::Low10p => range.5 | 0x08,
            XtalCap::High0p => range.6,
            XtalCap::Low0p => range.6 | 0x08,
        };
        rc = self.r82_write_reg_mask(0x10, val, 0x0b);
        if rc < 0 {
            return rc;
        }
        rc = self.r82_write_reg_mask(0x08, 0x00, 0x3f);
        if rc < 0 {
            return rc;
        }
        self.r82_write_reg_mask(0x09, 0x00, 0x3f)
    }

    fn r82_set_pll(&mut self, freq: u32) -> i32 {
        let vco_min: u32 = 1_770_000;
        let vco_max: u32 = vco_min * 2;
        let mut n_sdm: u16 = 2;
        let mut sdm: u16 = 0;
        let mut mix_div: u8 = 2;
        let mut div_num: u8 = 0;
        let mut vco_power_ref: u8 = 2;
        let refdiv2: u8 = 0;
        let mut data = [0u8; 5];

        // Frequency in kHz
        let freq_khz = freq.wrapping_add(500) / 1000;
        let pll_ref = self.r82.xtal;
        let pll_ref_khz = self.r82.xtal.wrapping_add(500) / 1000;

        let mut rc = self.r82_write_reg_mask(0x10, refdiv2, 0x10);
        if rc < 0 {
            return rc;
        }
        // set pll autotune = 128kHz
        rc = self.r82_write_reg_mask(0x1a, 0x00, 0x0c);
        if rc < 0 {
            return rc;
        }
        // RTL-SDR Blog Modification: Set VCO current to MAX
        rc = self.r82_write_reg_mask(0x12, 0x06, 0xff);
        if rc < 0 {
            return rc;
        }
        // Calculate divider
        while mix_div <= 64 {
            let p = freq_khz.wrapping_mul(mix_div as u32);
            if p >= vco_min && p < vco_max {
                let mut div_buf = mix_div;
                while div_buf > 2 {
                    div_buf >>= 1;
                    div_num = div_num.wrapping_add(1);
                }
                break;
            }
            mix_div = ((mix_div as u32) << 1) as u8;
        }
        rc = self.r82_read(0x00, &mut data);
        if rc < 0 {
            return rc;
        }
        if self.r82.chip == Chip::R828D || self.check_dongle_model("RTLSDRBlog", "Blog V4L") {
            vco_power_ref = 1;
        }
        let vco_fine_tune = (data[4] & 0x30) >> 4;
        if vco_fine_tune > vco_power_ref {
            div_num = div_num.wrapping_sub(1);
        } else if vco_fine_tune < vco_power_ref {
            div_num = div_num.wrapping_add(1);
        }
        rc = self.r82_write_reg_mask(0x10, ((div_num as u32) << 5) as u8, 0xe0);
        if rc < 0 {
            return rc;
        }
        let vco_freq: u64 = (freq as u64).wrapping_mul(mix_div as u64);
        let nint: u8 = (vco_freq / (pll_ref.wrapping_mul(2) as u64)) as u8;
        let mut vco_fra: u32 = (vco_freq.wrapping_sub(pll_ref.wrapping_mul(2).wrapping_mul(nint as u32) as u64) / 1000) as u32;
        if nint as i32 > (128 / vco_power_ref as i32) - 1 {
            // No valid PLL values
            return -1;
        }
        let ni: u8 = ((nint as i32 - 13) / 4) as u8;
        let si: u8 = (nint as i32 - 4 * ni as i32 - 13) as u8;
        rc = self.r82_write_reg(0x14, (ni as u32 + ((si as u32) << 6)) as u8);
        if rc < 0 {
            return rc;
        }
        // pw_sdm
        let val = if vco_fra == 0 { 0x08 } else { 0x00 };
        rc = self.r82_write_reg_mask(0x12, val, 0x08);
        if rc < 0 {
            return rc;
        }
        // sdm calculator
        while vco_fra > 1 {
            if n_sdm == 0 {
                break;
            }
            if vco_fra > (2 * pll_ref_khz) / n_sdm as u32 {
                sdm = sdm.wrapping_add((32768 / (n_sdm as u32 / 2)) as u16);
                vco_fra = vco_fra.wrapping_sub((2 * pll_ref_khz) / n_sdm as u32);
                if n_sdm >= 0x8000 {
                    break;
                }
            }
            n_sdm <<= 1;
        }
        rc = self.r82_write_reg(0x16, (sdm >> 8) as u8);
        if rc < 0 {
            return rc;
        }
        rc = self.r82_write_reg(0x15, (sdm & 0xff) as u8);
        if rc < 0 {
            return rc;
        }
        for i in 0..2 {
            // Check if PLL has locked
            rc = self.r82_read(0x00, &mut data[..3]);
            if rc < 0 {
                return rc;
            }
            if data[2] & 0x40 != 0 {
                break;
            }
            if i == 0 {
                // Didn't lock. RTL-SDR Blog Hack: Set max current
                rc = self.r82_write_reg_mask(0x12, 0x06, 0xff);
                if rc < 0 {
                    return rc;
                }
            }
        }
        if data[2] & 0x40 == 0 {
            // PLL not locked
            self.r82.has_lock = false;
            return 0;
        }
        self.r82.has_lock = true;
        // set pll autotune = 8kHz
        self.r82_write_reg_mask(0x1a, 0x08, 0x08)
    }

    /// `r82xx_sysfreq_sel` for the only call the driver makes: frequency 0, digital TV, DVB-T (the 8 MHz defaults).
    fn r82_sysfreq_sel(&mut self) -> i32 {
        let mixer_top = 0x24u8; // mixer top:13 , top-1, low-discharge
        let lna_top = 0xe5u8; // detect bw 3, lna top:4, predet top:2
        let cp_cur = 0x38u8; // 111, auto
        let lna_vth_l = 0x53u8; // lna vth 0.84 , vtl 0.64
        let mixer_vth_l = 0x75u8; // mixer vth 1.04, vtl 0.84
        let air_cable1_in = 0x00u8;
        let cable2_in = 0x00u8;
        let pre_dect = 0x40u8;
        let lna_discharge = 14u8;
        let filter_cur = 0x40u8; // 10, low
        let mut rc;

        if self.r82.use_predetect {
            rc = self.r82_write_reg_mask(0x06, pre_dect, 0x40);
            if rc < 0 {
                return rc;
            }
        }
        rc = self.r82_write_reg_mask(0x1d, lna_top, 0xc7);
        if rc < 0 {
            return rc;
        }
        rc = self.r82_write_reg_mask(0x1c, mixer_top, 0xf8);
        if rc < 0 {
            return rc;
        }
        rc = self.r82_write_reg(0x0d, lna_vth_l);
        if rc < 0 {
            return rc;
        }
        rc = self.r82_write_reg(0x0e, mixer_vth_l);
        if rc < 0 {
            return rc;
        }
        self.r82.input = air_cable1_in;
        // Air-IN only for Astrometa
        rc = self.r82_write_reg_mask(0x05, air_cable1_in, 0x60);
        if rc < 0 {
            return rc;
        }
        rc = self.r82_write_reg_mask(0x06, cable2_in, 0x08);
        if rc < 0 {
            return rc;
        }
        rc = self.r82_write_reg_mask(0x11, cp_cur, 0x38);
        if rc < 0 {
            return rc;
        }
        // RTL-SDR Blog Hack. Improve L-band performance by setting PLL drop out to 2.0v
        let div_buf_cur = 0xa0u8;
        rc = self.r82_write_reg_mask(0x17, div_buf_cur, 0x30);
        if rc < 0 {
            return rc;
        }
        rc = self.r82_write_reg_mask(0x0a, filter_cur, 0x60);
        if rc < 0 {
            return rc;
        }
        // Set LNA (type is digital TV)
        // LNA TOP: lowest
        rc = self.r82_write_reg_mask(0x1d, 0, 0x38);
        if rc < 0 {
            return rc;
        }
        // 0: normal mode
        rc = self.r82_write_reg_mask(0x1c, 0, 0x04);
        if rc < 0 {
            return rc;
        }
        // 0: PRE_DECT off
        rc = self.r82_write_reg_mask(0x06, 0, 0x40);
        if rc < 0 {
            return rc;
        }
        // agc clk 250hz
        rc = self.r82_write_reg_mask(0x1a, 0x30, 0x30);
        if rc < 0 {
            return rc;
        }
        // write LNA TOP = 3
        rc = self.r82_write_reg_mask(0x1d, 0x18, 0x38);
        if rc < 0 {
            return rc;
        }
        // write discharge mode (the mask is "wrong" in the original driver as well)
        rc = self.r82_write_reg_mask(0x1c, mixer_top, 0x04);
        if rc < 0 {
            return rc;
        }
        // LNA discharge current
        rc = self.r82_write_reg_mask(0x1e, lna_discharge, 0x1f);
        if rc < 0 {
            return rc;
        }
        // agc clk 60hz
        rc = self.r82_write_reg_mask(0x1a, 0x20, 0x30);
        if rc < 0 {
            return rc;
        }
        0
    }

    /// `r82xx_set_tv_standard` for the only call the driver makes: bandwidth below 6 MHz, digital TV.
    fn r82_set_tv_standard(&mut self) -> i32 {
        let if_khz: u32 = 3570;
        let filt_cal_lo: u32 = 56000; // 52000->56000
        let filt_gain = 0x30u8; // +3db, 6mhz on
        let img_r = 0x00u8; // image negative
        let filt_q = 0x10u8; // r10[4]:low q(1'b1)
        let hp_cor = 0x6bu8; // 1.7m disable, +2cap, 1.0mhz
        let ext_enable = 0x60u8; // r30[6]=1 ext enable; r30[5]:1 ext at lna max-1
        let loop_through = 0x80u8; // r5[7], lt off
        let lt_att = 0x00u8; // r31[7], lt att enable
        let flt_ext_widest = 0x00u8; // r15[7]: flt_ext_wide off
        let polyfil_cur = 0x60u8; // r25[6:5]:min
        let mut data = [0u8; 5];

        // Initialize the shadow registers
        self.r82.regs = INIT_ARRAY;
        // Init Flag & Xtal_check Result (inits VGA gain, needed?)
        let mut rc = self.r82_write_reg_mask(0x0c, 0x00, 0x0f);
        if rc < 0 {
            return rc;
        }
        // version
        rc = self.r82_write_reg_mask(0x13, VER_NUM, 0x3f);
        if rc < 0 {
            return rc;
        }
        // for LT Gain test (type is digital TV)
        rc = self.r82_write_reg_mask(0x1d, 0x00, 0x38);
        if rc < 0 {
            return rc;
        }
        self.r82.int_freq = if_khz * 1000;
        // The standard changed (we call this function only once): filter calibration is needed
        for _ in 0..2 {
            // Set filt_cap
            rc = self.r82_write_reg_mask(0x0b, hp_cor, 0x60);
            if rc < 0 {
                return rc;
            }
            // set cali clk =on
            rc = self.r82_write_reg_mask(0x0f, 0x04, 0x04);
            if rc < 0 {
                return rc;
            }
            // X'tal cap 0pF for PLL
            rc = self.r82_write_reg_mask(0x10, 0x00, 0x03);
            if rc < 0 {
                return rc;
            }
            rc = self.r82_set_pll(filt_cal_lo * 1000);
            if rc < 0 || !self.r82.has_lock {
                return rc;
            }
            // Start Trigger
            rc = self.r82_write_reg_mask(0x0b, 0x10, 0x10);
            if rc < 0 {
                return rc;
            }
            // Stop Trigger
            rc = self.r82_write_reg_mask(0x0b, 0x00, 0x10);
            if rc < 0 {
                return rc;
            }
            // set cali clk =off
            rc = self.r82_write_reg_mask(0x0f, 0x00, 0x04);
            if rc < 0 {
                return rc;
            }
            // Check if calibration worked
            rc = self.r82_read(0x00, &mut data);
            if rc < 0 {
                return rc;
            }
            self.r82.fil_cal_code = data[4] & 0x0f;
            if self.r82.fil_cal_code != 0 && self.r82.fil_cal_code != 0x0f {
                break;
            }
        }
        // narrowest
        if self.r82.fil_cal_code == 0x0f {
            self.r82.fil_cal_code = 0;
        }
        rc = self.r82_write_reg_mask(0x0a, filt_q | self.r82.fil_cal_code, 0x1f);
        if rc < 0 {
            return rc;
        }
        // Set BW, Filter_gain, & HP corner
        rc = self.r82_write_reg_mask(0x0b, hp_cor, 0xef);
        if rc < 0 {
            return rc;
        }
        // Set Img_R
        rc = self.r82_write_reg_mask(0x07, img_r, 0x80);
        if rc < 0 {
            return rc;
        }
        // Set filt_3dB, V6MHz
        rc = self.r82_write_reg_mask(0x06, filt_gain, 0x30);
        if rc < 0 {
            return rc;
        }
        // channel filter extension
        rc = self.r82_write_reg_mask(0x1e, ext_enable, 0x60);
        if rc < 0 {
            return rc;
        }
        // Loop through
        rc = self.r82_write_reg_mask(0x05, loop_through, 0x80);
        if rc < 0 {
            return rc;
        }
        // Loop through attenuation
        rc = self.r82_write_reg_mask(0x1f, lt_att, 0x80);
        if rc < 0 {
            return rc;
        }
        // filter extension widest
        rc = self.r82_write_reg_mask(0x0f, flt_ext_widest, 0x80);
        if rc < 0 {
            return rc;
        }
        // RF poly filter current
        rc = self.r82_write_reg_mask(0x19, polyfil_cur, 0x60);
        if rc < 0 {
            return rc;
        }
        0
    }

    /// Manual gain (`set_manual_gain`) with `gain` in tenths of a dB, or automatic gain (`r82xx_set_gain`).
    pub(crate) fn r82_set_gain(&mut self, set_manual_gain: bool, gain: i32) -> i32 {
        if set_manual_gain {
            let mut total_gain = 0i32;
            let mut mix_index = 0usize;
            let mut lna_index = 0usize;
            let mut data = [0u8; 4];
            // LNA auto off
            let mut rc = self.r82_write_reg_mask(0x05, 0x10, 0x10);
            if rc < 0 {
                return rc;
            }
            // Mixer auto off
            rc = self.r82_write_reg_mask(0x07, 0, 0x10);
            if rc < 0 {
                return rc;
            }
            rc = self.r82_read(0x00, &mut data);
            if rc < 0 {
                return rc;
            }
            rc = self.r82_set_vga_gain();
            if rc < 0 {
                return rc;
            }
            for _ in 0..15 {
                if total_gain >= gain {
                    break;
                }
                lna_index += 1;
                total_gain += LNA_GAIN_STEPS[lna_index];
                if total_gain >= gain {
                    break;
                }
                mix_index += 1;
                total_gain += MIXER_GAIN_STEPS[mix_index];
            }
            // set LNA gain
            rc = self.r82_write_reg_mask(0x05, lna_index as u8, 0x0f);
            if rc < 0 {
                return rc;
            }
            // set Mixer gain
            rc = self.r82_write_reg_mask(0x07, mix_index as u8, 0x0f);
            if rc < 0 {
                return rc;
            }
        } else {
            // LNA
            let mut rc = self.r82_write_reg_mask(0x05, 0, 0x10);
            if rc < 0 {
                return rc;
            }
            // Mixer
            rc = self.r82_write_reg_mask(0x07, 0x10, 0x10);
            if rc < 0 {
                return rc;
            }
            // set fixed VGA gain for now (26.5 dB)
            rc = self.r82_write_reg_mask(0x0c, 0x0b, 0x9f);
            if rc < 0 {
                return rc;
            }
        }
        0
    }

    /// Fixed VGA gain (16.3 dB) (`r82xx_set_vga_gain`).
    fn r82_set_vga_gain(&mut self) -> i32 {
        self.r82_write_reg_mask(0x0c, 0x08, 0x9f)
    }

    /// IF filter bandwidth; returns the IF frequency in Hz the demodulator must use, or a negative error (`r82xx_set_bandwidth`).
    pub(crate) fn r82_set_bandwidth(&mut self, bw: i32) -> i32 {
        let mut bw = bw;
        let mut real_bw = 0i32;
        let reg_0a: u8;
        let mut reg_0b: u8;
        if bw > 7_000_000 {
            // BW: 8 MHz
            reg_0a = 0x10;
            reg_0b = 0x0b;
            self.r82.int_freq = 4_570_000;
        } else if bw > 6_000_000 {
            // BW: 7 MHz
            reg_0a = 0x10;
            reg_0b = 0x2a;
            self.r82.int_freq = 4_570_000;
        } else if bw > IF_LOW_PASS_BW_TABLE[0] + FILT_HP_BW1 + FILT_HP_BW2 {
            // BW: 6 MHz
            reg_0a = 0x10;
            reg_0b = 0x6b;
            self.r82.int_freq = 3_570_000;
        } else {
            reg_0a = 0x00;
            reg_0b = 0x80;
            self.r82.int_freq = 2_300_000;
            if bw > IF_LOW_PASS_BW_TABLE[0] + FILT_HP_BW1 {
                bw -= FILT_HP_BW2;
                self.r82.int_freq = self.r82.int_freq.wrapping_add(FILT_HP_BW2 as u32);
                real_bw += FILT_HP_BW2;
            } else {
                reg_0b |= 0x20;
            }
            if bw > IF_LOW_PASS_BW_TABLE[0] {
                bw -= FILT_HP_BW1;
                self.r82.int_freq = self.r82.int_freq.wrapping_add(FILT_HP_BW1 as u32);
                real_bw += FILT_HP_BW1;
            } else {
                reg_0b |= 0x40;
            }
            // find low-pass filter
            let mut i = 0usize;
            while i < IF_LOW_PASS_BW_TABLE.len() {
                if bw > IF_LOW_PASS_BW_TABLE[i] {
                    break;
                }
                i += 1;
            }
            // (the C decrements an unsigned index; a break at i == 0 cannot happen after the reductions above)
            let i = i.saturating_sub(1);
            reg_0b |= (15 - i) as u8;
            real_bw += IF_LOW_PASS_BW_TABLE[i];
            self.r82.int_freq = self.r82.int_freq.wrapping_sub((real_bw / 2) as u32);
        }
        let mut rc = self.r82_write_reg_mask(0x0a, reg_0a, 0x10);
        if rc < 0 {
            return rc;
        }
        rc = self.r82_write_reg_mask(0x0b, reg_0b, 0xef);
        if rc < 0 {
            return rc;
        }
        self.r82.int_freq as i32
    }

    /// Tunes to `freq` Hz, switching the inputs of an RTL-SDR Blog V4 or an R828D (`r82xx_set_freq`).
    pub(crate) fn r82_set_freq(&mut self, freq: u32) -> i32 {
        let is_v4 = self.check_dongle_model("RTLSDRBlog", "Blog V4");
        let is_v4l = self.check_dongle_model("RTLSDRBlog", "Blog V4L");
        // an RTL-SDR Blog V4 or V4 Lite (R820T / R828S tuner, USB product "Blog V4L") upconverts by 28.8 MHz when tuned to HF
        let upconvert_freq = if (is_v4 || is_v4l) && freq < 28_800_000 { freq.wrapping_add(28_800_000) } else { freq };
        let lo_freq = upconvert_freq.wrapping_add(self.r82.int_freq);

        let mut rc = self.r82_set_mux(lo_freq);
        if rc < 0 {
            return rc;
        }
        rc = self.r82_set_vga_gain();
        if rc < 0 {
            return rc;
        }
        rc = self.r82_set_pll(lo_freq);
        if rc < 0 || !self.r82.has_lock {
            return rc;
        }
        if is_v4 {
            // notches are OFF when tuned within the notch band and ON outside it
            let open_d = if freq <= 2_200_000 || (85_000_000..=112_000_000).contains(&freq) || (172_000_000..=242_000_000).contains(&freq) {
                0x00
            } else {
                0x08
            };
            rc = self.r82_write_reg_mask(0x17, open_d, 0x08);
            if rc < 0 {
                return rc;
            }
            // select tuner band based on frequency and only switch if there is a band change
            let band = if freq <= 28_800_000 {
                HF
            } else if freq > 28_800_000 && freq < 250_000_000 {
                VHF
            } else {
                UHF
            };
            // bypass the tracking filter on HF: the upconverter path does not use it (set_mux re-applies it on every frequency change)
            if band == HF {
                rc = self.r82_write_reg_mask(0x1a, 0x40, 0xc3);
                if rc < 0 {
                    return rc;
                }
                rc = self.r82_write_reg(0x1b, 0x00);
                if rc < 0 {
                    return rc;
                }
            }
            // switch between tuner inputs on the RTL-SDR Blog V4
            if band != self.r82.input {
                self.r82.input = band;
                // activate cable 2 (HF input)
                let cable_2_in = if band == HF { 0x08 } else { 0x00 };
                rc = self.r82_write_reg_mask(0x06, cable_2_in, 0x08);
                if rc < 0 {
                    return rc;
                }
                // Control upconverter GPIO switch on newer batches
                rc = self.set_bias_tee_gpio(5, cable_2_in == 0);
                if rc < 0 {
                    return rc;
                }
                // activate cable 1 (VHF input)
                let cable_1_in = if band == VHF { 0x40 } else { 0x00 };
                rc = self.r82_write_reg_mask(0x05, cable_1_in, 0x40);
                if rc < 0 {
                    return rc;
                }
                // activate air_in (UHF input)
                let air_in = if band == UHF { 0x00 } else { 0x20 };
                rc = self.r82_write_reg_mask(0x05, air_in, 0x20);
                if rc < 0 {
                    return rc;
                }
            }
        } else if is_v4l {
            // the V4 Lite has the HF input on cable 1 and the UHF input on air-in only
            let band = if freq <= 28_800_000 { HF } else { UHF };
            if band == HF {
                rc = self.r82_write_reg_mask(0x1a, 0x40, 0xc3);
                if rc < 0 {
                    return rc;
                }
                rc = self.r82_write_reg(0x1b, 0x00);
                if rc < 0 {
                    return rc;
                }
            }
            if band != self.r82.input {
                self.r82.input = band;
                let cable_1_in = if band == HF { 0x40 } else { 0x00 };
                // upconverter GPIO switch on newer batches
                rc = self.set_bias_tee_gpio(5, cable_1_in == 0);
                if rc < 0 {
                    return rc;
                }
                rc = self.r82_write_reg_mask(0x05, cable_1_in, 0x40);
                if rc < 0 {
                    return rc;
                }
                let air_in = if band == UHF { 0x00 } else { 0x20 };
                rc = self.r82_write_reg_mask(0x05, air_in, 0x20);
                if rc < 0 {
                    return rc;
                }
            }
        } else {
            // Standard R828D dongle: switch between 'Cable1' and 'Air-In' at 345 MHz
            let air_cable1_in = if freq > 345_000_000 { 0x00 } else { 0x60 };
            if self.r82.chip == Chip::R828D && air_cable1_in != self.r82.input {
                self.r82.input = air_cable1_in;
                rc = self.r82_write_reg_mask(0x05, air_cable1_in, 0x60);
            }
        }
        rc
    }

    /// Puts the tuner in standby (`r82xx_standby`).
    pub(crate) fn r82_standby(&mut self) -> i32 {
        // If device was not initialized yet, don't need to standby
        if !self.r82.init_done {
            return 0;
        }
        const SEQ: [(u8, u8); 11] = [
            (0x06, 0xb1),
            (0x05, 0xa0),
            (0x07, 0x3a),
            (0x08, 0x40),
            (0x09, 0xc0),
            (0x0a, 0x36),
            (0x0c, 0x35),
            (0x0f, 0x68),
            (0x11, 0x03),
            (0x17, 0xf4),
            (0x19, 0x0c),
        ];
        let mut rc = 0;
        for (reg, val) in SEQ {
            rc = self.r82_write_reg(reg, val);
            if rc < 0 {
                return rc;
            }
        }
        rc
    }

    /// Initialises the tuner registers (`r82xx_init`).
    pub(crate) fn r82_init(&mut self) -> i32 {
        // TODO in the C: R828D might need r82xx_xtal_check()
        self.r82.xtal_cap_sel = XtalCap::High0p;
        // Initialize registers (the result is not checked in the C either)
        let _ = self.r82_write(0x05, &INIT_ARRAY);
        let rc = self.r82_set_tv_standard();
        if rc < 0 {
            return rc;
        }
        let rc = self.r82_sysfreq_sel();
        self.r82.init_done = true;
        rc
    }

    // ---- the entries of the tuner table (`r820t_*` in librtlsdr.c) ----------------------------------------------------------------------------------

    pub(crate) fn r820t_init(&mut self) -> i32 {
        if self.tuner_type == crate::rtl::Tuner::R828D {
            self.r82.i2c_addr = R828D_I2C_ADDR;
            self.r82.chip = Chip::R828D;
        } else {
            self.r82.i2c_addr = R820T_I2C_ADDR;
            self.r82.chip = Chip::R820T;
        }
        self.r82.xtal = self.get_xtal_freq().1;
        self.r82.max_i2c_msg_len = 8;
        self.r82.use_predetect = false;
        self.r82_init()
    }

    pub(crate) fn r820t_exit(&mut self) -> i32 {
        self.r82_standby()
    }

    pub(crate) fn r820t_set_freq(&mut self, freq: u32) -> i32 {
        self.r82_set_freq(freq)
    }

    pub(crate) fn r820t_set_bw(&mut self, bw: i32) -> i32 {
        let r = self.r82_set_bandwidth(bw);
        if r < 0 {
            return r;
        }
        let r = self.set_if_freq(r as u32);
        if r != 0 {
            return r;
        }
        self.set_center_freq(self.freq)
    }

    pub(crate) fn r820t_set_gain(&mut self, gain: i32) -> i32 {
        self.r82_set_gain(true, gain)
    }

    pub(crate) fn r820t_set_gain_mode(&mut self, manual: i32) -> i32 {
        self.r82_set_gain(manual != 0, 0)
    }
}
