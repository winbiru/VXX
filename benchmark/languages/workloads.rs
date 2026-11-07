// Cùng ABI và bộ đo với C; tính i32 tràn bằng phép wrapping tường minh.
#[unsafe(no_mangle)]
pub extern "C" fn native_arithmetic(k: i32, _: i32, _: i32, _: i32) -> i32 {
    k.wrapping_add(17).wrapping_mul(31)
        .wrapping_add(k.wrapping_mul(7).wrapping_add(3))
        .wrapping_sub(k.wrapping_add(5).wrapping_mul(11))
}
#[unsafe(no_mangle)]
pub extern "C" fn native_branch_loop(k: i32, _: i32, _: i32, _: i32) -> i32 {
    let mut x = k.wrapping_mul(97).wrapping_add(11);
    for _ in 0..256 {
        x = x.wrapping_add(251);
        if x >= 65521 { x = x.wrapping_sub(65521); }
    }
    x
}
#[unsafe(no_mangle)]
pub extern "C" fn native_fibonacci(k: i32, _: i32, _: i32, _: i32) -> i32 {
    let (mut a, mut b) = (0i32, 1i32);
    for _ in 0..k % 16 + 20 { let next = a.wrapping_add(b); a = b; b = next; }
    a
}
