setJitCompilerOption("baseline.warmup.trigger", 10);
setJitCompilerOption("ion.warmup.trigger", 20);
var i;

// Check that we are able to remove the operation inside recover test functions (denoted by "rop..."),
// when we inline the first version of uceFault, and ensure that the bailout is correct
// when uceFault is replaced (which cause an invalidation bailout)

var uceFault = function (i) {
    if (i > 98)
        uceFault = function (i) { return true; };
    return false;
}

var uceFault_bitnot_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_bitnot_number'));
function rbitnot_number(i) {
    var x = ~i;
    if (uceFault_bitnot_number(i) || uceFault_bitnot_number(i))
        assertEq(x, -100  /* = ~99 */);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_bitnot_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_bitnot_object'));
function rbitnot_object(i) {
    var t = i;
    var o = { valueOf: function () { return t; } };
    var x = ~o; /* computed with t == i, not 1000 */
    t = 1000;
    if (uceFault_bitnot_object(i) || uceFault_bitnot_object(i))
        assertEq(x, -100  /* = ~99 */);
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_bitand_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_bitand_number'));
function rbitand_number(i) {
    var x = 1 & i;
    if (uceFault_bitand_number(i) || uceFault_bitand_number(i))
        assertEq(x, 1  /* = 1 & 99 */);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_bitand_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_bitand_object'));
function rbitand_object(i) {
    var t = i;
    var o = { valueOf: function () { return t; } };
    var x = o & i; /* computed with t == i, not 1000 */
    t = 1000;
    if (uceFault_bitand_object(i) || uceFault_bitand_object(i))
        assertEq(x, 99);
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_bitor_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_bitor_number'));
function rbitor_number(i) {
    var x = i | -100; /* -100 == ~99 */
    if (uceFault_bitor_number(i) || uceFault_bitor_number(i))
        assertEq(x, -1) /* ~99 | 99 = -1 */
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_bitor_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_bitor_object'));
function rbitor_object(i) {
    var t = i;
    var o = { valueOf: function() { return t; } };
    var x = o | -100;
    t = 1000;
    if (uceFault_bitor_object(i) || uceFault_bitor_object(i))
        assertEq(x, -1);
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_bitxor_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_bitxor_number'));
function rbitxor_number(i) {
    var x = 1 ^ i;
    if (uceFault_bitxor_number(i) || uceFault_bitxor_number(i))
        assertEq(x, 98  /* = 1 XOR 99 */);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_bitxor_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_bitxor_object'));
function rbitxor_object(i) {
    var t = i;
    var o = { valueOf: function () { return t; } };
    var x = 1 ^ o; /* computed with t == i, not 1000 */
    t = 1000;
    if (uceFault_bitxor_object(i) || uceFault_bitxor_object(i))
        assertEq(x, 98  /* = 1 XOR 99 */);
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_lsh_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_lsh_number'));
function rlsh_number(i) {
    var x = i << 1;
    if (uceFault_lsh_number(i) || uceFault_lsh_number(i))
        assertEq(x, 198); /* 99 << 1 == 198 */
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_lsh_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_lsh_object'));
function rlsh_object(i) {
    var t = i;
    var o = { valueOf: function() { return t; } };
    var x = o << 1;
    t = 1000;
    if (uceFault_lsh_object(i) || uceFault_lsh_object(i))
        assertEq(x, 198);
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_rsh_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_rsh_number'));
function rrsh_number(i) {
    var x = i >> 1;
    if (uceFault_rsh_number(i) || uceFault_rsh_number(i))
        assertEq(x, 49  /* = 99 >> 1 */);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_rsh_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_rsh_object'));
function rrsh_object(i) {
    var t = i;
    var o = { valueOf: function () { return t; } };
    var x = o >> 1; /* computed with t == i, not 1000 */
    t = 1000;
    if (uceFault_rsh_object(i) || uceFault_rsh_object(i))
        assertEq(x, 49  /* = 99 >> 1 */);
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_ursh_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_ursh_number'));
function rursh_number(i) {
    var x = i >>> 1;
    if (uceFault_ursh_number(i) || uceFault_ursh_number(i))
        assertEq(x, 49  /* = 99 >>> 1 */);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_ursh_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_ursh_object'));
function rursh_object(i) {
    var t = i;
    var o = { valueOf: function () { return t; } };
    var x = o >>> 1; /* computed with t == i, not 1000 */
    t = 1000;
    if (uceFault_ursh_object(i) || uceFault_ursh_object(i))
        assertEq(x, 49  /* = 99 >>> 1 */);
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_add_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_add_number'));
function radd_number(i) {
    var x = 1 + i;
    if (uceFault_add_number(i) || uceFault_add_number(i))
        assertEq(x, 100  /* = 1 + 99 */);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_add_float = eval(uneval(uceFault).replace('uceFault', 'uceFault_add_float'));
function radd_float(i) {
    var t = Math.fround(1/3);
    var fi = Math.fround(i);
    var x = Math.fround(Math.fround(Math.fround(Math.fround(t + fi) + t) + fi) + t);
    if (uceFault_add_float(i) || uceFault_add_float(i))
        assertEq(x, 199); /* != 199.00000002980232 (when computed with double additions) */
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_add_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_add_object'));
function radd_object(i) {
    var t = i;
    var o = { valueOf: function () { return t; } };
    var x = o + i; /* computed with t == i, not 1000 */
    t = 1000;
    if (uceFault_add_object(i) || uceFault_add_object(i))
        assertEq(x, 198);
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_sub_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_sub_number'));
function rsub_number(i) {
    var x = 1 - i;
    if (uceFault_sub_number(i) || uceFault_sub_number(i))
        assertEq(x, -98  /* = 1 - 99 */);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_sub_float = eval(uneval(uceFault).replace('uceFault', 'uceFault_sub_float'));
function rsub_float(i) {
    var t = Math.fround(1/3);
    var fi = Math.fround(i);
    var x = Math.fround(Math.fround(Math.fround(Math.fround(t - fi) - t) - fi) - t);
    if (uceFault_sub_float(i) || uceFault_sub_float(i))
        assertEq(x, -198.3333282470703); /* != -198.33333334326744 (when computed with double subtractions) */
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_sub_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_sub_object'));
function rsub_object(i) {
    var t = i;
    var o = { valueOf: function () { return t; } };
    var x = o - i; /* computed with t == i, not 1000 */
    t = 1000;
    if (uceFault_sub_object(i) || uceFault_sub_object(i))
        assertEq(x, 0);
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_mul_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_mul_number'));
function rmul_number(i) {
    var x = 2 * i;
    if (uceFault_mul_number(i) || uceFault_mul_number(i))
        assertEq(x, 198  /* = 1 * 99 */);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_mul_overflow = eval(uneval(uceFault).replace('uceFault', 'uceFault_mul_overflow'));
function rmul_overflow(i) {
    var x = Math.pow(2, i * 16 / 99) | 0;
    x = x * x;
    if (uceFault_mul_overflow(i) || uceFault_mul_overflow(i))
        assertEq(x, Math.pow(2, 32));
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_mul_float = eval(uneval(uceFault).replace('uceFault', 'uceFault_mul_float'));
function rmul_float(i) {
    var t = Math.fround(1/3);
    var fi = Math.fround(i);
    var x = Math.fround(Math.fround(Math.fround(Math.fround(t * fi) * t) * fi) * t);
    if (uceFault_mul_float(i) || uceFault_mul_float(i))
        assertEq(x, 363); /* != 363.0000324547301 (when computed with double multiplications) */
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_mul_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_mul_object'));
function rmul_object(i) {
    var t = i;
    var o = { valueOf: function () { return t; } };
    var x = o * i; /* computed with t == i, not 1000 */
    t = 1000;
    if (uceFault_mul_object(i) || uceFault_mul_object(i))
        assertEq(x, 9801);
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_imul_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_imul_number'));
function rimul_number(i) {
    var x = Math.imul(2, i);
    if (uceFault_imul_number(i) || uceFault_imul_number(i))
        assertEq(x, 198  /* = 1 * 99 */);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_imul_overflow = eval(uneval(uceFault).replace('uceFault', 'uceFault_imul_overflow'));
function rimul_overflow(i) {
    var x = Math.pow(2, i * 16 / 99) | 0;
    x = Math.imul(x, x);
    if (uceFault_imul_overflow(i) || uceFault_imul_overflow(i))
        assertEq(x, 0);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_imul_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_imul_object'));
function rimul_object(i) {
    var t = i;
    var o = { valueOf: function () { return t; } };
    var x = Math.imul(o, i); /* computed with t == i, not 1000 */
    t = 1000;
    if (uceFault_imul_object(i) || uceFault_imul_object(i))
        assertEq(x, 9801);
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_div_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_div_number'));
function rdiv_number(i) {
    var x = 1 / i;
    if (uceFault_div_number(i) || uceFault_div_number(i))
        assertEq(x, 0.010101010101010102  /* = 1 / 99 */);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_div_float = eval(uneval(uceFault).replace('uceFault', 'uceFault_div_float'));
function rdiv_float(i) {
    var t = Math.fround(1/3);
    var fi = Math.fround(i);
    var x = Math.fround(Math.fround(Math.fround(Math.fround(t / fi) / t) / fi) / t);
    if (uceFault_div_float(i) || uceFault_div_float(i))
        assertEq(x, 0.0003060912131331861); /* != 0.0003060912060598955 (when computed with double divisions) */
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_div_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_div_object'));
function rdiv_object(i) {
    var t = i;
    var o = { valueOf: function () { return t; } };
    var x = o / i; /* computed with t == i, not 1000 */
    t = 1000;
    if (uceFault_div_object(i) || uceFault_div_object(i))
        assertEq(x, 1);
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_mod_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_mod_number'));
function rmod_number(i) {
    var x = i % 98;
    if (uceFault_mod_number(i) || uceFault_mod_number(i))
        assertEq(x, 1); /* 99 % 98 = 1 */
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_mod_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_mod_object'));
function rmod_object(i) {
    var t = i;
    var o = { valueOf: function() { return t; } };
    var x = o % 98; /* computed with t == i, not 1000 */
    t = 1000;
    if(uceFault_mod_object(i) || uceFault_mod_object(i))
        assertEq(x, 1); /* 99 % 98 = 1 */
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_not_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_not_number'));
function rnot_number(i) {
    var x = !i;
    if (uceFault_not_number(i) || uceFault_not_number(i))
        assertEq(x, false /* = !99 */);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_not_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_not_object'));
function rnot_object(i) {
    var o = objectEmulatingUndefined();
    var x = !o;
    if(uceFault_not_object(i) || uceFault_not_object(i))
        assertEq(x, true /* = !undefined = !document.all = !objectEmulatingUndefined() */);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_concat_string = eval(uneval(uceFault).replace('uceFault', 'uceFault_concat_string'));
function rconcat_string(i) {
    var x = "s" + i.toString();
    if (uceFault_concat_string(i) || uceFault_concat_string(i))
        assertEq(x, "s99");
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_concat_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_concat_number'));
function rconcat_number(i) {
    var x = "s" + i;
    if (uceFault_concat_number(i) || uceFault_concat_number(i))
        assertEq(x, "s99");
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_string_length = eval(uneval(uceFault).replace('uceFault', 'uceFault_string_length'));
function rstring_length(i) {
    var x = i.toString().length;
    if (uceFault_string_length(i) || uceFault_string_length(i))
        assertEq(x, 2);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_arguments_length_1 = eval(uneval(uceFault).replace('uceFault', 'uceFault_arguments_length_1'));
function rarguments_length_1(i) {
    var x = arguments.length;
    if (uceFault_arguments_length_1(i) || uceFault_arguments_length_1(i))
        assertEq(x, 1);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_arguments_length_3 = eval(uneval(uceFault).replace('uceFault', 'uceFault_arguments_length_3'));
function rarguments_length_3(i) {
    var x = arguments.length;
    if (uceFault_arguments_length_3(i) || uceFault_arguments_length_3(i))
        assertEq(x, 3);
    assertRecoveredOnBailout(x, true);
    return i;
}

function ret_argumentsLength() { return arguments.length; }

var uceFault_inline_arguments_length_1 = eval(uneval(uceFault).replace('uceFault', 'uceFault_inline_arguments_length_1'));
function rinline_arguments_length_1(i) {
    var x = ret_argumentsLength.apply(this, arguments);
    if (uceFault_inline_arguments_length_1(i) || uceFault_inline_arguments_length_1(i))
        assertEq(x, 1);
    // We cannot garantee that the function would be inlined
    // assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_inline_arguments_length_3 = eval(uneval(uceFault).replace('uceFault', 'uceFault_inline_arguments_length_3'));
function rinline_arguments_length_3(i) {
    var x = ret_argumentsLength.apply(this, arguments);
    if (uceFault_inline_arguments_length_3(i) || uceFault_inline_arguments_length_3(i))
        assertEq(x, 3);
    // We cannot garantee that the function would be inlined
    // assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_floor_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_floor_number'));
function rfloor_number(i) {
    var x = Math.floor(i + 0.1111);
    if (uceFault_floor_number(i) || uceFault_floor_number(i))
        assertEq(x, i);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_floor_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_floor_object'));
function rfloor_object(i) {
    var t = i + 0.1111;
    var o = { valueOf: function () { return t; } };
    var x = Math.floor(o);
    t = 1000.1111;
    if (uceFault_floor_object(i) || uceFault_floor_object(i))
        assertEq(x, i);
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_ceil_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_ceil_number'));
function rceil_number(i) {
    var x = Math.ceil(-i - 0.12010799100);
    if (uceFault_ceil_number(i) || uceFault_ceil_number(i))
        assertEq(x, -i);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_round_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_round'));
function rround_number(i) {
    var x = Math.round(i + 1.4);
    if (uceFault_round_number(i) || uceFault_round_number(i))
        assertEq(x, 100); /* = i + 1*/
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_round_double = eval(uneval(uceFault).replace('uceFault', 'uceFault_round_double'));
function rround_double(i) {
    var x = Math.round(i + (-1 >>> 0));
    if (uceFault_round_double(i) || uceFault_round_double(i))
        assertEq(x, 99 + (-1 >>> 0)); /* = i + 2 ^ 32 - 1 */
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_Char_Code_At = eval(uneval(uceFault).replace('uceFault', 'uceFault_Char_Code_At'));
function rcharCodeAt(i) {
    var s = "aaaaa";
    var x = s.charCodeAt(i % 4);
    if (uceFault_Char_Code_At(i) || uceFault_Char_Code_At(i))
        assertEq(x, 97 );
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_from_char_code = eval(uneval(uceFault).replace('uceFault', 'uceFault_from_char_code'));
function rfrom_char_code(i) {
    var x = String.fromCharCode(i);
    if (uceFault_from_char_code(i) || uceFault_from_char_code(i))
        assertEq(x, "c");
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_from_char_code_non_ascii = eval(uneval(uceFault).replace('uceFault', 'uceFault_from_char_code_non_ascii'));
function rfrom_char_code_non_ascii(i) {
    var x = String.fromCharCode(i * 100);
    if (uceFault_from_char_code_non_ascii(i) || uceFault_from_char_code_non_ascii(i))
        assertEq(x, "\u26AC");
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_pow_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_pow_number'));
function rpow_number(i) {
    var x = Math.pow(i, 3.14159);
    if (uceFault_pow_number(i) || uceFault_pow_number(i))
        assertEq(x, Math.pow(99, 3.14159));
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_pow_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_pow_object'));
function rpow_object(i) {
    var t = i;
    var o = { valueOf: function () { return t; } };
    var x = Math.pow(o, 3.14159); /* computed with t == i, not 1.5 */
    t = 1.5;
    if (uceFault_pow_object(i) || uceFault_pow_object(i))
        assertEq(x, Math.pow(99, 3.14159));
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_powhalf_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_powhalf_number'));
function rpowhalf_number(i) {
    var x = Math.pow(i, 0.5);
    if (uceFault_powhalf_number(i) || uceFault_powhalf_number(i))
        assertEq(x, Math.pow(99, 0.5));
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_powhalf_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_powhalf_object'));
function rpowhalf_object(i) {
    var t = i;
    var o = { valueOf: function () { return t; } };
    var x = Math.pow(o, 0.5); /* computed with t == i, not 1.5 */
    t = 1.5;
    if (uceFault_powhalf_object(i) || uceFault_powhalf_object(i))
        assertEq(x, Math.pow(99, 0.5));
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_min_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_min_number'));
function rmin_number(i) {
    var x = Math.min(i, i-1, i-2.1);
    if (uceFault_min_number(i) || uceFault_min_number(i))
        assertEq(x, i-2.1);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_min_float = eval(uneval(uceFault).replace('uceFault', 'uceFault_min_float'));
function rmin_float(i) {
    var x = Math.fround(Math.min(Math.fround(i), Math.fround(13.37)));
    if (uceFault_min_number(i) || uceFault_min_number(i))
        assertEq(x, Math.fround(13.37));
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_min_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_min_object'));
function rmin_object(i) {
    var t = i;
    var o = { valueOf: function () { return t; } };
    var x = Math.min(o, o-1, o-2.1)
    t = 1000;
    if (uceFault_min_object(i) || uceFault_min_object(i))
        assertEq(x, i-2.1);
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_max_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_max_number'));
function rmax_number(i) {
    var x = Math.max(i, i-1, i-2.1);
    if (uceFault_max_number(i) || uceFault_max_number(i))
        assertEq(x, i);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_max_float = eval(uneval(uceFault).replace('uceFault', 'uceFault_max_float'));
function rmax_float(i) {
    var x = Math.fround(Math.max(Math.fround(-i), Math.fround(13.37)));
    if (uceFault_max_number(i) || uceFault_max_number(i))
        assertEq(x, Math.fround(13.37));
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_max_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_max_object'));
function rmax_object(i) {
    var t = i;
    var o = { valueOf: function () { return t; } };
    var x = Math.max(o, o-1, o-2.1)
    t = 1000;
    if (uceFault_max_object(i) || uceFault_max_object(i))
        assertEq(x, i);
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_abs = eval(uneval(uceFault).replace('uceFault', 'uceFault_abs'));
function rabs_number(i) {
    var x = Math.abs(i-42);
    if (uceFault_abs(i) || uceFault_abs(i))
        assertEq(x, 57);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_abs_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_abs_object'));
function rabs_object(i) {
    var t = -i;
    var o = { valueOf: function() { return t; } };
    var x = Math.abs(o); /* computed with t == i, not 1000 */
    t = 1000;
    if(uceFault_abs_object(i) || uceFault_abs_object(i))
        assertEq(x, 99);
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_sqrt_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_sqrt_number'));
function rsqrt_number(i) {
    var x = Math.sqrt(i);
    if (uceFault_sqrt_number(i) || uceFault_sqrt_number(i))
        assertEq(x, Math.sqrt(99));
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_sqrt_float = eval(uneval(uceFault).replace('uceFault', 'uceFault_sqrt_float'));
function rsqrt_float(i) {
    var x = Math.fround(Math.sqrt(Math.fround(i)));
    if (uceFault_sqrt_float(i) || uceFault_sqrt_float(i))
        assertEq(x, Math.fround(Math.sqrt(Math.fround(99)))); /* != 9.9498743710662 (when computed with double sqrt) */
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_sqrt_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_sqrt_object'));
function rsqrt_object(i) {
    var t = i;
    var o = { valueOf: function () { return t; } };
    var x = Math.sqrt(o); /* computed with t == i, not 1.5 */
    t = 1.5;
    if (uceFault_sqrt_object(i) || uceFault_sqrt_object(i))
        assertEq(x, Math.sqrt(99));
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_atan2_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_atan2_number'));
function ratan2_number(i) {
    var x = Math.atan2(i, i+1);
    if (uceFault_atan2_number(i) || uceFault_atan2_number(i))
        assertEq(x, Math.atan2(99, 100));
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_atan2_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_atan2_object'));
function ratan2_object(i) {
    var t = i;
    var o = { valueOf: function () { return t; } };
    var x = Math.atan2(o, o+1);
    t = 1000;
    if (uceFault_atan2_object(i) || uceFault_atan2_object(i))
        assertEq(x, Math.atan2(i, i+1));
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_str_split = eval(uneval(uceFault).replace('uceFault', 'uceFault_str_split'))
function rstr_split(i) {
    var x = "str01234567899876543210rts".split("" + i);
    if (uceFault_str_split(i) || uceFault_str_split(i))
        assertEq(x[0], "str012345678");
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_regexp_exec = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_exec'))
function rregexp_exec(i) {
    var re = new RegExp("(str)\\d+" + i + "\\d+rts");
    var res = re.exec("str01234567899876543210rts");
    if (uceFault_regexp_exec(i) || uceFault_regexp_exec(i))
        assertEq(res[1], "str");
    assertRecoveredOnBailout(res, false);
    return i;
}
var uceFault_regexp_y_exec = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_y_exec'))
function rregexp_y_exec(i) {
    var re = new RegExp("(str)\\d+" + (i % 10), "y");
    var res = re.exec("str00123456789");
    if (uceFault_regexp_y_exec(i) || uceFault_regexp_y_exec(i))
        assertEq(res[1], "str");
    assertRecoveredOnBailout(res, false);
    assertEq(re.lastIndex == 0, false);
    return i;
}

var uceFault_regexp_y_literal_exec = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_y_literal_exec'))
function rregexp_y_literal_exec(i) {
    var re = /(str)\d*0/y;
    var res = re.exec("str00123456789");
    if (uceFault_regexp_y_literal_exec(i) || uceFault_regexp_y_literal_exec(i))
        assertEq(res[1], "str");
    assertRecoveredOnBailout(res, false);
    assertEq(re.lastIndex == 0, false);
    return i;
}

var uceFault_regexp_g_exec = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_g_exec'))
function rregexp_g_exec(i) {
    var re = new RegExp("(str)\\d+" + (i % 10), "g");
    var res = re.exec("str00123456789str00123456789");
    if (uceFault_regexp_g_exec(i) || uceFault_regexp_g_exec(i))
        assertEq(res[1], "str");
    assertRecoveredOnBailout(res, false);
    assertEq(re.lastIndex == 0, false);
    return i;
}

var uceFault_regexp_g_literal_exec = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_g_literal_exec'))
function rregexp_g_literal_exec(i) {
    var re = /(str)\d*0/g;
    var res = re.exec("str00123456789str00123456789");
    if (uceFault_regexp_g_literal_exec(i) || uceFault_regexp_g_literal_exec(i))
        assertEq(res[1], "str");
    assertRecoveredOnBailout(res, false);
    assertEq(re.lastIndex == 0, false);
    return i;
}

var uceFault_regexp_i_exec = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_i_exec'))
function rregexp_i_exec(i) {
    var re = new RegExp("(str)\\d+" + (i % 10), "i");
    var res = re.exec("STR00123456789");
    if (uceFault_regexp_i_exec(i) || uceFault_regexp_i_exec(i))
        assertEq(res[1], "STR");
    assertRecoveredOnBailout(res, false);
    assertEq(re.lastIndex == 0, true);
    return i;
}

var uceFault_regexp_i_literal_exec = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_i_literal_exec'))
function rregexp_i_literal_exec(i) {
    var re = /(str)\d*0/i;
    var res = re.exec("STR00123456789");
    if (uceFault_regexp_i_literal_exec(i) || uceFault_regexp_i_literal_exec(i))
        assertEq(res[1], "STR");
    assertRecoveredOnBailout(res, false);
    assertEq(re.lastIndex == 0, true);
    return i;
}


var uceFault_regexp_m_exec = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_m_exec'))
function rregexp_m_exec(i) {
    var re = new RegExp("^(str)\\d+" + (i % 10), "m");
    var res = re.exec("abc\nstr00123456789");
    if (uceFault_regexp_m_exec(i) || uceFault_regexp_m_exec(i))
        assertEq(res[1], "str");
    assertRecoveredOnBailout(res, false);
    assertEq(re.lastIndex == 0, true);
    return i;
}

var uceFault_regexp_m_literal_exec = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_m_literal_exec'))
function rregexp_m_literal_exec(i) {
    var re = /^(str)\d*0/m;
    var res = re.exec("abc\nstr00123456789");
    if (uceFault_regexp_m_literal_exec(i) || uceFault_regexp_m_literal_exec(i))
        assertEq(res[1], "str");
    assertRecoveredOnBailout(res, false);
    assertEq(re.lastIndex == 0, true);
    return i;
}

var uceFault_regexp_test = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_test'))
function rregexp_test(i) {
    var re = new RegExp("str\\d+" + i + "\\d+rts");
    var res = re.test("str01234567899876543210rts");
    if (uceFault_regexp_test(i) || uceFault_regexp_test(i))
        assertEq(res, true);
    assertRecoveredOnBailout(res, false);
    return i;
}

var uceFault_regexp_y_test = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_y_test'))
function rregexp_y_test(i) {
    var re = new RegExp("str\\d+" + (i % 10), "y");
    var res = re.test("str00123456789");
    if (uceFault_regexp_y_test(i) || uceFault_regexp_y_test(i))
        assertEq(res, true);
    assertRecoveredOnBailout(res, false);
    assertEq(re.lastIndex == 0, false);
    return i;
}

var uceFault_regexp_y_literal_test = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_y_literal_test'))
function rregexp_y_literal_test(i) {
    var re = /str\d*0/y;
    var res = re.test("str00123456789");
    if (uceFault_regexp_y_literal_test(i) || uceFault_regexp_y_literal_test(i))
        assertEq(res, true);
    assertRecoveredOnBailout(res, false);
    assertEq(re.lastIndex == 0, false);
    return i;
}

var uceFault_regexp_g_test = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_g_test'))
function rregexp_g_test(i) {
    var re = new RegExp("str\\d+" + (i % 10), "g");
    var res = re.test("str00123456789str00123456789");
    if (uceFault_regexp_g_test(i) || uceFault_regexp_g_test(i))
        assertEq(res, true);
    assertRecoveredOnBailout(res, false);
    assertEq(re.lastIndex == 0, false);
    return i;
}

var uceFault_regexp_g_literal_test = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_g_literal_test'))
function rregexp_g_literal_test(i) {
    var re = /str\d*0/g;
    var res = re.test("str00123456789str00123456789");
    if (uceFault_regexp_g_literal_test(i) || uceFault_regexp_g_literal_test(i))
        assertEq(res, true);
    assertRecoveredOnBailout(res, false);
    assertEq(re.lastIndex == 0, false);
    return i;
}

var uceFault_regexp_i_test = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_i_test'))
function rregexp_i_test(i) {
    var re = new RegExp("str\\d+" + (i % 10), "i");
    var res = re.test("STR00123456789");
    if (uceFault_regexp_i_test(i) || uceFault_regexp_i_test(i))
        assertEq(res, true);
    assertRecoveredOnBailout(res, false);
    assertEq(re.lastIndex == 0, true);
    return i;
}

var uceFault_regexp_i_literal_test = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_i_literal_test'))
function rregexp_i_literal_test(i) {
    var re = /str\d*0/i;
    var res = re.test("STR00123456789");
    if (uceFault_regexp_i_literal_test(i) || uceFault_regexp_i_literal_test(i))
        assertEq(res, true);
    assertRecoveredOnBailout(res, false);
    assertEq(re.lastIndex == 0, true);
    return i;
}

var uceFault_regexp_m_test = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_m_test'))
function rregexp_m_test(i) {
    var re = new RegExp("^str\\d+" + (i % 10), "m");
    var res = re.test("abc\nstr00123456789");
    if (uceFault_regexp_m_test(i) || uceFault_regexp_m_test(i))
        assertEq(res, true);
    assertRecoveredOnBailout(res, false);
    assertEq(re.lastIndex == 0, true);
    return i;
}

var uceFault_regexp_m_literal_test = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_m_literal_test'))
function rregexp_m_literal_test(i) {
    var re = /^str\d*0/m;
    var res = re.test("abc\nstr00123456789");
    if (uceFault_regexp_m_literal_test(i) || uceFault_regexp_m_literal_test(i))
        assertEq(res, true);
    assertRecoveredOnBailout(res, false);
    assertEq(re.lastIndex == 0, true);
    return i;
}

var uceFault_regexp_replace = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_replace'))
function rregexp_replace(i) {
    var re = new RegExp("str\\d+" + (i % 10));
    var res = "str00123456789".replace(re, "abc");
    if (uceFault_regexp_replace(i) || uceFault_regexp_replace(i))
        assertEq(res, "abc");
    assertRecoveredOnBailout(res, false);
    return i;
}

var uceFault_regexp_y_replace = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_y_replace'))
function rregexp_y_replace(i) {
    var re = new RegExp("str\\d+" + (i % 10), "y");
    re.test("str00123456789");
    assertEq(re.lastIndex == 0, false);

    var res = "str00123456789".replace(re, "abc");

    assertEq(re.lastIndex, 0);

    assertEq(res, "str00123456789");

    res = "str00123456789".replace(re, "abc");
    assertEq(re.lastIndex == 0, false);

    if (uceFault_regexp_y_replace(i) || uceFault_regexp_y_replace(i))
        assertEq(res, "abc");
    assertRecoveredOnBailout(res, false);
    return i;
}

var uceFault_regexp_y_literal_replace = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_y_literal_replace'))
function rregexp_y_literal_replace(i) {
    var re = /str\d+9/y;
    re.test("str00123456789");
    assertEq(re.lastIndex == 0, false);

    var res = "str00123456789".replace(re, "abc");

    assertEq(re.lastIndex, 0);

    assertEq(res, "str00123456789");

    res = "str00123456789".replace(re, "abc");
    assertEq(re.lastIndex == 0, false);

    if (uceFault_regexp_y_literal_replace(i) || uceFault_regexp_y_literal_replace(i))
        assertEq(res, "abc");
    assertRecoveredOnBailout(res, false);
    return i;
}

var uceFault_regexp_g_replace = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_g_replace'))
function rregexp_g_replace(i) {
    var re = new RegExp("str\\d+" + (i % 10), "g");
    re.test("str00123456789");
    assertEq(re.lastIndex == 0, false);

    var res = "str00123456789".replace(re, "abc");

    // replace will always zero the lastIndex field, even if it was not zero before.
    assertEq(re.lastIndex == 0, true);

    if (uceFault_regexp_g_replace(i) || uceFault_regexp_g_replace(i))
        assertEq(res, "abc");
    assertRecoveredOnBailout(res, false);
    return i;
}

var uceFault_regexp_g_literal_replace = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_g_literal_replace'))
function rregexp_g_literal_replace(i) {
    var re = /str\d+9/g;
    re.test("str00123456789");
    assertEq(re.lastIndex == 0, false);

    var res = "str00123456789".replace(re, "abc");

    // replace will zero the lastIndex field.
    assertEq(re.lastIndex == 0, true);

    if (uceFault_regexp_g_literal_replace(i) || uceFault_regexp_g_literal_replace(i))
        assertEq(res, "abc");
    assertRecoveredOnBailout(res, false);
    return i;
}

var uceFault_regexp_i_replace = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_i_replace'))
function rregexp_i_replace(i) {
    var re = new RegExp("str\\d+" + (i % 10), "i");
    re.test("STR00123456789");
    assertEq(re.lastIndex == 0, true);

    var res = "STR00123456789".replace(re, "abc");

    assertEq(re.lastIndex == 0, true);

    if (uceFault_regexp_i_replace(i) || uceFault_regexp_i_replace(i))
        assertEq(res, "abc");
    assertRecoveredOnBailout(res, false);
    return i;
}

var uceFault_regexp_i_literal_replace = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_i_literal_replace'))
function rregexp_i_literal_replace(i) {
    var re = /str\d+9/i;
    re.test("STR00123456789");
    assertEq(re.lastIndex == 0, true);

    var res = "str00123456789".replace(re, "abc");

    assertEq(re.lastIndex == 0, true);

    if (uceFault_regexp_i_literal_replace(i) || uceFault_regexp_i_literal_replace(i))
        assertEq(res, "abc");
    assertRecoveredOnBailout(res, false);
    return i;
}

var uceFault_regexp_m_replace = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_m_replace'))
function rregexp_m_replace(i) {
    var re = new RegExp("^str\\d+" + (i % 10), "m");
    re.test("abc\nstr00123456789");
    assertEq(re.lastIndex == 0, true);

    var res = "abc\nstr00123456789".replace(re, "abc");

    assertEq(re.lastIndex == 0, true);

    if (uceFault_regexp_m_replace(i) || uceFault_regexp_m_replace(i))
        assertEq(res, "abc\nabc");
    assertRecoveredOnBailout(res, false);
    return i;
}

var uceFault_regexp_m_literal_replace = eval(uneval(uceFault).replace('uceFault', 'uceFault_regexp_m_literal_replace'))
function rregexp_m_literal_replace(i) {
    var re = /^str\d+9/m;
    re.test("abc\nstr00123456789");
    assertEq(re.lastIndex == 0, true);

    var res = "abc\nstr00123456789".replace(re, "abc");

    assertEq(re.lastIndex == 0, true);

    if (uceFault_regexp_m_literal_replace(i) || uceFault_regexp_m_literal_replace(i))
        assertEq(res, "abc\nabc");
    assertRecoveredOnBailout(res, false);
    return i;
}

var uceFault_string_replace = eval(uneval(uceFault).replace('uceFault', 'uceFault_string_replace'))
function rstring_replace(i) {
    var re = /str\d+9/;

    assertEq(re.lastIndex == 0, true);
    var res = "str00123456789".replace(re, "abc");
    if (uceFault_string_replace(i) || uceFault_string_replace(i))
        assertEq(res, "abc");
    assertRecoveredOnBailout(res, false);
    assertEq(re.lastIndex == 0, true);
    return i;
}

var uceFault_string_replace_y = eval(uneval(uceFault).replace('uceFault', 'uceFault_string_replace_y'))
function rstring_replace_y(i) {
    var re = /str\d+9/y;

    assertEq(re.lastIndex == 0, true);
    var res = "str00123456789".replace(re, "abc");
    if (uceFault_string_replace_y(i) || uceFault_string_replace_y(i))
        assertEq(res, "abc");
    assertRecoveredOnBailout(res, false);
    assertEq(re.lastIndex == 0, false);
    return i;
}

var uceFault_string_replace_g = eval(uneval(uceFault).replace('uceFault', 'uceFault_string_replace_g'))
function rstring_replace_g(i) {
    var re = /str\d+9/g;

    assertEq(re.lastIndex == 0, true);
    var res = "str00123456789str00123456789".replace(re, "abc");
    if (uceFault_string_replace_g(i) || uceFault_string_replace_g(i))
        assertEq(res, "abcabc");
    assertRecoveredOnBailout(res, false);
    assertEq(re.lastIndex == 0, true);
    return i;
}

var uceFault_typeof = eval(uneval(uceFault).replace('uceFault', 'uceFault_typeof'))
function rtypeof(i) {
    var inputs = [ {}, [], 1, true, undefined, function(){}, null ];
    var types = [ "object", "object", "number", "boolean", "undefined", "function", "object"];
    if (typeof Symbol === "function") {
      inputs.push(Symbol());
      types.push("symbol");
    }
    var x = typeof (inputs[i % inputs.length]);
    var y = types[i % types.length];

    if (uceFault_typeof(i) || uceFault_typeof(i))
        assertEq(x, y);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_todouble_value = eval(uneval(uceFault).replace('uceFault', 'uceFault_todouble_value'))
function rtodouble_value(i) {
    var a = 1;
    if (i == 1000) a = "1";

    var x = a < 8.1;

    if (uceFault_todouble_value(i) || uceFault_todouble_value(i))
        assertEq(x, true);
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_todouble_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_todouble_number'));
function rtodouble_number(i) {
    var x = Math.fround(Math.fround(i) + Math.fround(i)) + 1;
    if (uceFault_todouble_number(i) || uceFault_todouble_number(i))
        assertEq(2 * i + 1, x);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_tofloat32_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_tofloat32_number'));
function rtofloat32_number(i) {
    var x = Math.fround(i + 0.1111111111);
    if (uceFault_tofloat32_number(i) || uceFault_tofloat32_number(i))
        assertEq(x, Math.fround(99.1111111111));
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_tofloat32_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_tofloat32_object'));
function rtofloat32_object(i) {
    var t = i + 0.1111111111;
    var o = { valueOf: function () { return t; } };
    var x = Math.fround(o);
    t = 1000.1111111111;
    if (uceFault_tofloat32_object(i) || uceFault_tofloat32_object(i))
        assertEq(x, Math.fround(99.1111111111));
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_trunc_to_int32_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_trunc_to_int32_number'));
function rtrunc_to_int32_number(i) {
    var x = (i + 0.12) | 0;
    if (uceFault_trunc_to_int32_number(i) || uceFault_trunc_to_int32_number(i))
        assertEq(x, (i + 0.12) | 0);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_trunc_to_int32_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_trunc_to_int32_object'));
function rtrunc_to_int32_object(i) {
    var t1 = i + 0.12;
    var o1 = { valueOf: function() { return t1; } };
    var x = o1 | 0;
    t1 = 777.12;
    if (uceFault_trunc_to_int32_object(i) || uceFault_trunc_to_int32_object(i))
        assertEq(x, (i + 0.12) | 0);
    assertRecoveredOnBailout(x, false);
}

var uceFault_trunc_to_int32_string = eval(uneval(uceFault).replace('uceFault', 'uceFault_trunc_to_int32_string'));
function rtrunc_to_int32_string(i) {
    var x = (i + "0") | 0;
    if (uceFault_trunc_to_int32_string(i) || uceFault_trunc_to_int32_string(i))
        assertEq(x, (i + "0") | 0);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_hypot_number_2args = eval(uneval(uceFault).replace('uceFault', 'uceFault_hypot_number_2args'));
function rhypot_number_2args(i) {
    var x = Math.hypot(i, i + 1);
    if (uceFault_hypot_number_2args(i) || uceFault_hypot_number_2args(i))
        assertEq(x, Math.sqrt(i * i + (i + 1) * (i + 1)));
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_hypot_number_3args = eval(uneval(uceFault).replace('uceFault', 'uceFault_hypot_number_3args'));
function rhypot_number_3args(i) {
    var x = Math.hypot(i, i + 1, i + 2);
    if (uceFault_hypot_number_3args(i) || uceFault_hypot_number_3args(i))
        assertEq(x, Math.sqrt(i * i + (i + 1) * (i + 1) + (i + 2) * (i + 2)));
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_hypot_number_4args = eval(uneval(uceFault).replace('uceFault', 'uceFault_hypot_number_4args'));
function rhypot_number_4args(i) {
    var x = Math.hypot(i, i + 1, i + 2, i + 3);
    if (uceFault_hypot_number_4args(i) || uceFault_hypot_number_4args(i))
        assertEq(x, Math.sqrt(i * i + (i + 1) * (i + 1) + (i + 2) * (i + 2) + (i + 3) * (i + 3)));
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_hypot_object_2args = eval(uneval(uceFault).replace('uceFault', 'uceFault_hypot_object_2args'));
function rhypot_object_2args(i) {
    var t0 = i;
    var t1 = i + 1;
    var o0 = { valueOf: function () { return t0; } };
    var o1 = { valueOf: function () { return t1; } };
    var x = Math.hypot(o0, o1);
    t0 = 1000;
    t1 = 2000;
    if (uceFault_hypot_object_2args(i) || uceFault_hypot_object_2args(i) )
        assertEq(x, Math.sqrt(i * i + (i + 1) * (i + 1)));
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_hypot_object_3args = eval(uneval(uceFault).replace('uceFault', 'uceFault_hypot_object_3args'));
function rhypot_object_3args(i) {
    var t0 = i;
    var t1 = i + 1;
    var t2 = i + 2;
    var o0 = { valueOf: function () { return t0; } };
    var o1 = { valueOf: function () { return t1; } };
    var o2 = { valueOf: function () { return t2; } };
    var x = Math.hypot(o0, o1, o2);
    t0 = 1000;
    t1 = 2000;
    t2 = 3000;
    if (uceFault_hypot_object_3args(i) || uceFault_hypot_object_3args(i) )
        assertEq(x, Math.sqrt(i * i + (i + 1) * (i + 1) + (i + 2) * (i + 2)));
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_hypot_object_4args = eval(uneval(uceFault).replace('uceFault', 'uceFault_hypot_object_4args'));
function rhypot_object_4args(i) {
    var t0 = i;
    var t1 = i + 1;
    var t2 = i + 2;
    var t3 = i + 3;
    var o0 = { valueOf: function () { return t0; } };
    var o1 = { valueOf: function () { return t1; } };
    var o2 = { valueOf: function () { return t2; } };
    var o3 = { valueOf: function () { return t3; } };
    var x = Math.hypot(o0, o1, o2, o3);
    t0 = 1000;
    t1 = 2000;
    t2 = 3000;
    t3 = 4000;
    if (uceFault_hypot_object_4args(i) || uceFault_hypot_object_4args(i) )
        assertEq(x, Math.sqrt(i * i + (i + 1) * (i + 1) + (i + 2) * (i + 2) + (i + 3) * (i + 3)));
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_sin_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_sin_number'));
function rsin_number(i) {
    var x = Math.sin(i);
    if (uceFault_sin_number(i) || uceFault_sin_number(i))
        assertEq(x, Math.sin(i));
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_sin_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_sin_object'));
function rsin_object(i) {
    var t = i;
    var o = { valueOf: function() { return t; } };
    var x = Math.sin(o);
    t = 777;
    if (uceFault_sin_object(i) || uceFault_sin_object(i))
        assertEq(x, Math.sin(i));
    assertRecoveredOnBailout(x, false);
    return i;
}

var uceFault_log_number = eval(uneval(uceFault).replace('uceFault', 'uceFault_log_number'));
function rlog_number(i) {
    var x = Math.log(i);
    if (uceFault_log_number(i) || uceFault_log_number(i))
        assertEq(x, Math.log(99) /* log(99) */);
    assertRecoveredOnBailout(x, true);
    return i;
}

var uceFault_log_object = eval(uneval(uceFault).replace('uceFault', 'uceFault_log_object'));
function rlog_object(i) {
    var t = i;
    var o = { valueOf: function() { return t; } };
    var x = Math.log(o); /* Evaluated with t == i, not t == 1000 */
    t = 1000;
    if (uceFault_log_object(i) || uceFault_log_object(i))
        assertEq(x, Math.log(99) /* log(99) */);
    assertRecoveredOnBailout(x, false);
    return i;
}

for (i = 0; i < 100; i++) {
    rbitnot_number(i);
    rbitnot_object(i);
    rbitand_number(i);
    rbitand_object(i);
    rbitor_number(i);
    rbitor_object(i);
    rbitxor_number(i);
    rbitxor_object(i);
    rlsh_number(i);
    rlsh_object(i);
    rrsh_number(i);
    rrsh_object(i);
    rursh_number(i);
    rursh_object(i);
    radd_number(i);
    radd_float(i);
    radd_object(i);
    rsub_number(i);
    rsub_float(i);
    rsub_object(i);
    rmul_number(i);
    rmul_overflow(i);
    rmul_float(i);
    rmul_object(i);
    rimul_number(i);
    rimul_overflow(i);
    rimul_object(i);
    rdiv_number(i);
    rdiv_float(i);
    rdiv_object(i);
    rmod_number(i);
    rmod_object(i);
    rnot_number(i);
    rnot_object(i);
    rconcat_string(i);
    rconcat_number(i);
    rstring_length(i);
    rarguments_length_1(i);
    rarguments_length_3(i, 0, 1);
    rinline_arguments_length_1(i);
    rinline_arguments_length_3(i, 0, 1);
    rfloor_number(i);
    rfloor_object(i);
    rceil_number(i);
    rround_number(i);
    rround_double(i);
    rcharCodeAt(i);
    rfrom_char_code(i);
    rfrom_char_code_non_ascii(i);
    rpow_number(i);
    rpow_object(i);
    rpowhalf_number(i);
    rpowhalf_object(i);
    rmin_number(i);
    rmin_float(i);
    rmin_object(i);
    rmax_number(i);
    rmax_float(i);
    rmax_object(i);
    rabs_number(i);
    rabs_object(i);
    rsqrt_number(i);
    rsqrt_float(i);
    rsqrt_object(i);
    ratan2_number(i);
    ratan2_object(i);
    rstr_split(i);
    rregexp_exec(i);
    rregexp_y_exec(i);
    rregexp_y_literal_exec(i);
    rregexp_g_exec(i);
    rregexp_g_literal_exec(i);
    rregexp_i_exec(i);
    rregexp_i_literal_exec(i);
    rregexp_m_exec(i);
    rregexp_m_literal_exec(i);
    rregexp_test(i);
    rregexp_y_test(i);
    rregexp_y_literal_test(i);
    rregexp_g_test(i);
    rregexp_g_literal_test(i);
    rregexp_i_test(i);
    rregexp_i_literal_test(i);
    rregexp_m_test(i);
    rregexp_m_literal_test(i);
    rregexp_replace(i);
    rregexp_y_replace(i);
    rregexp_y_literal_replace(i);
    rregexp_g_replace(i);
    rregexp_g_literal_replace(i);
    rregexp_i_replace(i);
    rregexp_i_literal_replace(i);
    rregexp_m_replace(i);
    rregexp_m_literal_replace(i);
    rstring_replace(i);
    rstring_replace_y(i);
    rstring_replace_g(i);
    rtypeof(i);
    rtodouble_value(i);
    rtodouble_number(i);
    rtofloat32_number(i);
    rtofloat32_object(i);
    rtrunc_to_int32_number(i);
    rtrunc_to_int32_object(i);
    rtrunc_to_int32_string(i);
    rhypot_number_2args(i);
    rhypot_number_3args(i);
    rhypot_number_4args(i);
    rhypot_object_2args(i);
    rhypot_object_3args(i);
    rhypot_object_4args(i);
    rsin_number(i);
    rsin_object(i);
    rlog_number(i);
    rlog_object(i);
}

// Test that we can refer multiple time to the same recover instruction, as well
// as chaining recover instructions.

function alignedAlloc($size, $alignment) {
    var $1 = $size + 4 | 0;
    var $2 = $alignment - 1 | 0;
    var $3 = $1 + $2 | 0;
    var $4 = malloc($3);
}

function malloc($bytes) {
    var $189 = undefined;
    var $198 = $189 + 8 | 0;
}

for (i = 0; i < 50; i++)
    alignedAlloc(608, 16);
