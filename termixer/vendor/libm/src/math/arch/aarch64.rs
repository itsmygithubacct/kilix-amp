
use core::arch::asm;

pub fn fma(mut x: f64, y: f64, z: f64) -> f64 {
        unsafe {
        asm!(
            "fmadd {x:d}, {x:d}, {y:d}, {z:d}",
            x = inout(vreg) x,
            y = in(vreg) y,
            z = in(vreg) z,
            options(nomem, nostack, pure)
        );
    }
    x
}

pub fn fmaf(mut x: f32, y: f32, z: f32) -> f32 {
        unsafe {
        asm!(
            "fmadd {x:s}, {x:s}, {y:s}, {z:s}",
            x = inout(vreg) x,
            y = in(vreg) y,
            z = in(vreg) z,
            options(nomem, nostack, pure)
        );
    }
    x
}

// NB: `frintx` is technically the correct instruction for C's `rint`. However, in Rust (and LLVM
// by default), `rint` is identical to `roundeven` (no fpenv interaction) so we use the
// side-effect-free `frintn`.
//
// In general, C code that calls Rust's libm should assume that fpenv is ignored.

pub fn rint(mut x: f64) -> f64 {
        //
    // `frintn` is always round-to-nearest which does not match the C specification, but Rust does
    // not support rounding modes.
    unsafe {
        asm!(
            "frintn {x:d}, {x:d}",
            x = inout(vreg) x,
            options(nomem, nostack, pure)
        );
    }
    x
}

pub fn rintf(mut x: f32) -> f32 {
        //
    // `frintn` is always round-to-nearest which does not match the C specification, but Rust does
    // not support rounding modes.
    unsafe {
        asm!(
            "frintn {x:s}, {x:s}",
            x = inout(vreg) x,
            options(nomem, nostack, pure)
        );
    }
    x
}

#[cfg(all(f16_enabled, target_feature = "fp16"))]
pub fn rintf16(mut x: f16) -> f16 {
        //
    // `frintn` is always round-to-nearest which does not match the C specification, but Rust does
    // not support rounding modes.
    unsafe {
        asm!(
            "frintn {x:h}, {x:h}",
            x = inout(vreg) x,
            options(nomem, nostack, pure)
        );
    }
    x
}

pub fn sqrt(mut x: f64) -> f64 {
        unsafe {
        asm!(
            "fsqrt {x:d}, {x:d}",
            x = inout(vreg) x,
            options(nomem, nostack, pure)
        );
    }
    x
}

pub fn sqrtf(mut x: f32) -> f32 {
        unsafe {
        asm!(
            "fsqrt {x:s}, {x:s}",
            x = inout(vreg) x,
            options(nomem, nostack, pure)
        );
    }
    x
}

#[cfg(all(f16_enabled, target_feature = "fp16"))]
pub fn sqrtf16(mut x: f16) -> f16 {
        // side effects.
    unsafe {
        asm!(
            "fsqrt {x:h}, {x:h}",
            x = inout(vreg) x,
            options(nomem, nostack, pure)
        );
    }
    x
}
