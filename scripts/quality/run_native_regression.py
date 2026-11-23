#!/usr/bin/env python3
"""Run V++ source regressions in JIT, or explicitly exercise the native toolchain."""
import argparse
import collections
import concurrent.futures
import contextlib
import hashlib
import threading
import json
import math
import os
import platform
import re
import signal
import struct
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "scripts/test"
MANIFEST = TESTS / "regression.json"
FIXTURE_EXECUTABLE = None

# Explicitly reviewed positive fixtures. Entrypoint, compiler, import-resolution,
# diagnostic and other context-sensitive probes remain independent executables.
# Each suite has at most nine entries so the dispatcher uses one ASCII digit.
MODULE_SUITES = {
    "core": ("bo_qua", "boolean", "ngoai_le", "nhanh_dieu_kien_moi",
             "so_thuc", "thoat_vong_lap", "toan_tu_moi"),
    "stdlib": ("stdlib", "stdlib_tinh_toan", "stdlib_mo_rong", "bo_noi_chuoi",
               "stdlib_nen_tang", "gia_tri", "stdlib_crypto"),
    "text": ("codec_uuid_bitset", "quet_tach_chuoi", "quoc_te_hoa"),
    "json": ("json_phan_tich", "json_dieu_khien", "json_escape_span",
             "json_key_scan", "json_utf8", "json_byte_view", "json_integer_overflow_scan"),
    "network": ("ffi_socket", "goi_dau_noi_mang", "goi_http",
                "stdlib_http", "stdlib_http_post_put"),
    "files": ("ffi_tep", "ffi_thu_muc", "loi_tep_co_cau_truc", "file_dem",
              "file_stream_oop", "stdlib_io_config_time"),
    "collections": ("collections_oop", "ngan_xep_oop"),
    "application": ("goi_dung", "nhat_ky", "stdlib_starter"),
    "time": ("goi_thoi_gian", "goi_thoi_gian_1_0"),
}
HARNESS_KEYS = ("dump", "linker_phase", "vm_phase", "cmake")
JIT_I32_HARNESSES = {
    "checknativeprint.cmake", "checknativebulk.cmake", "checknativebytefile.cmake",
    "checknativebytesops.cmake", "checknativeread.cmake",
    "checknativelexer.cmake", "checknativeparser.cmake", "checknativecodegen.cmake",
}
# The legacy mixed backend uses this allowlist for reviewed JIT fixtures.
JIT_VM_PHASES = {"core", "directory", "system", "process", "resolver", "socket", "socket_negative", "tls"}
JIT_CASE_KEYS = {"id", "suite", "source", "expected", "expected_stderr", "stdout", "exit", "smoke", "jit", "platforms"}

# Reviewed runtime-error cases also check an exact exit status and diagnostic regex.
INTERPRETED_RUNTIME_DIAGNOSTICS = {
    "kiem_tra_de_quy_vuot_gioi_han", "kiem_tra_loi_can_so_nguyen",
    "kiem_tra_loi_chia_du_so_thuc",
    "kiem_tra_loi_chi_so_vuot_pham_vi", "kiem_tra_loi_du_lieu_khong_the_danh_chi_so",
    "kiem_tra_loi_giam_sai_kieu", "kiem_tra_loi_kieu_chi_so",
    "kiem_tra_loi_phep_tinh_can_so", "kiem_tra_loi_so_sanh_khac_kieu",
    "kiem_tra_loi_opcode_tien_trinh",
}

# Reviewed, self-contained programs with no imports or filesystem dependencies.
# Pin the reviewed source contents so edited fixtures fall back to private copies.
# Normalize line endings for Windows checkouts.
FIXTURE_FREE_SOURCES = {
    "scripts/test/fixtures/native_i32_backend_calls.vi": "a59d48cdd69cba6a26545f6df8578a3583ef4368c3deb38ee2e0e7a7bc4b47db",
    "scripts/test/fixtures/native_i32_direct_calls.vi": "8f7372583175636980fdb94d3aa9960c58ec96a263bf47fe3f7dc997350dd1da",
    "scripts/test/fixtures/native_i32_expression_calls.vi": "ac0200c8fdfd16c9801f2cd6767e4862a4f0ce82f60aadbcf614248118e11e6c",
    "scripts/test/kiem_tra_loi_chia_du_so_thuc.vi": "a9f1d45440933099acf0b88a83efb410d8bb6434fb78e2d293201eb380171c0a",
    "scripts/test/fixtures/native_aot_index.vi": "48128ed494fc4f4252596b8a5f27dfa759bd47cc34b48881874c76314764a62f",
    "scripts/test/fixtures/native_aot_private_method.vi": "c1ceb6aaec2df8676d0217d7c2232ed1d1c603ca16ab849a1ac3e33e36bdd2e0",
    "scripts/test/fixtures/native_frame_pool_gc.vi": "00e318b0489397e44d94989939e037c13c9034f216b77e344db7358b06cb8f4a",
    "scripts/test/fixtures/native_i32_call_chain.vi": "852893d811986895f227b12c07bdb86011c244d3b4c4a75c1f5392a1c50b0b9d",
    "scripts/test/fixtures/native_i32_expression.vi": "0f73bb62239cef139b499e834a4b9bf5b3110739fed31d67fccd76c8993164f5",
    "scripts/test/fixtures/native_i32_nested_calls.vi": "55ea6c30afad9af3f3476bb1e85720bf8dbfd425252eba5a243de7bf879e43af",
    "scripts/test/kiem_tra_de_quy_vuot_gioi_han.vi": "e6699248bfa010b342ff78d1b7827c89db8664670f27e7c3f5e4554f2c2f7866",
    "scripts/test/kiem_tra_loi_can_so_nguyen.vi": "cb45efe086ea279da3fba46df0e5af896ac1f3c444b73414358f0bafecdde0c0",
    "scripts/test/kiem_tra_loi_chi_so_vuot_pham_vi.vi": "ed4eaa17e9bbf715585d07d6843cbfe5c4e8e86137970ccaacba71611564c647",
    "scripts/test/kiem_tra_loi_du_lieu_khong_the_danh_chi_so.vi": "a1e1a0a8100aa25c3e1adff153209f23e034421faa9c68026e1e71f028d394d0",
    "scripts/test/kiem_tra_loi_giam_sai_kieu.vi": "1dce701d0c8e07988ead8362f91c516df610d73522a22fd15457ab88e6aefcac",
    "scripts/test/kiem_tra_loi_kieu_chi_so.vi": "70e09d955a2f76514591e160f2d3774a1836fad7bd6b57c8616b5ef5800609a8",
    "scripts/test/kiem_tra_loi_phep_tinh_can_so.vi": "ac722837ec132aa1a2952fac001bb60d9ae04ecb9b5c675ce5e18eb044671b65",
    "scripts/test/kiem_tra_loi_so_sanh_khac_kieu.vi": "bc56bbc85e71ae5089cfe87698aa4642fc9f90e411cd32e2f5beaf0385d4d1e8",
    "scripts/test/kiem_tra_loi_opcode_tien_trinh.vi": "c61da7d16c8229b56b7d090cf4bb37c098cb6e83e2f3a88e22edb68c80b735db",
    "scripts/test/fixtures/native_exit_scalar.vi": "de165b049c19703100686619350ef5691f2498ba690f27748209030343c8df50",
    "scripts/test/fixtures/native_return_semantics.vi": "1d17f633992f50078054ee14ae4044edd85f1e4969296bcc78b32db4e3135ccc",
    "scripts/test/fixtures/native_protected_loop.vi": "01e8b155c56c6ff4c17b199e78324b33e7d43956dcc290a5e53949107662b4d7",
    "scripts/test/kiem_tra_aot_decimal_scan.vi": "64aa8be4980601e5e50aafda56a28ff927f1c87cf3bba02caf894210b6c8ce61",
    "scripts/test/kiem_tra_aot_type_branch.vi": "57e86e867c3328e96027c753d20afb8dcf9cb20255308a65c1f015c9feb39968",
    "scripts/test/kiem_tra_aot_utf8_word_scan.vi": "c8833edb1a8608e48b90f4247606b809d5ef5d9c86c8eccd7c0045337e5d2c0f",
    "scripts/test/fixtures/anonymous_functions/entry.vi": "f974f9a59d7ce32cf77dacd5ea5de93ea527dfcdfa5bfc23541ce4cb71e962aa",
    "scripts/test/kiem_tra_bo_qua.vi": "218d0c7d1b5157e1beafc51f77d23a0c980392aaa084c1bc08558d32b53d6ab3",
    "scripts/test/kiem_tra_boolean.vi": "e6d7898243106c450555a6a77e468ba9722b302f4ee9853809b530c2c36eb091",
    "scripts/test/kiem_tra_chon_ca.vi": "a2d6119bb2e841c66ff3426a9d572efe82b142f4bb1294460256933767acc93e",
    "scripts/test/kiem_tra_closure_capture.vi": "7cf4a63b7015f9a28c6a9acc0e273d2cf78ffc503c1d5b7f2b8358639a3ec77c",
    "scripts/test/kiem_tra_constructor_tham_so.vi": "89b60f1fa693505210472b8ba1ab4d9486aae42ebb1b9e92020ee7d2f1313d4e",
    "scripts/test/kiem_tra_cu_phap_modifier_cu.vi": "881512484544aeb0f3a3c690e6552b069c3d552208a7421621adf144a1105d69",
    "scripts/test/kiem_tra_de_quy.vi": "2d3eac2ae4880898859386192c430ba178ab72c1f49ca076e7edad077c30b299",
    "scripts/test/kiem_tra_giao_dien_trien_khai.vi": "b42be5053ad4f6c360f620aba62980c12713f95b7605a4cb728b6877a925625c",
    "scripts/test/kiem_tra_hoi_quy_tong_hop.vi": "7cc67a2c0a990c33a31ca1cba88f5a10b7ad440910e6e22ba11448d551d5c5db",
    "scripts/test/kiem_tra_khai_bao_bien_runtime.vi": "71fe57295404ae675f695cc1058cafa576128dc328a3312b7b1f364334978a3c",
    "scripts/test/kiem_tra_kieu_dong_call_boundary.vi": "2bb06ec26ddb72e4f7d2079e1d82214c5a726c3f4e03323360c5051a4f120598",
    "scripts/test/kiem_tra_dieu_kien_long_nhieu_cap.vi": "a28dad6deb59644cb5d7bd774a1ad4a798d4d3aa1caa1304fc837d09979ac0d1",
    "scripts/test/kiem_tra_dieu_kien_phu_dinh.vi": "d7b144d944a3480e5bad10821ba3b9e308a64597bf1a4f2c8e3ed2302c5b8474",
    "scripts/test/kiem_tra_ham.vi": "4e2323e821d89420572d9f4801b5986028f4683ed9148a70eeaa3c7b622c26a0",
    "scripts/test/kiem_tra_ham_4_tham_so.vi": "8acf108ea414316b3c44761a85c7db3219f8d27021df090c70fe6e2b7cf8e2e4",
    "scripts/test/kiem_tra_ham_da_tu_khong_nhay.vi": "0e33127b7d020d55a4b82d59a77def58a88caea354be355994c3d932b7c52988",
    "scripts/test/kiem_tra_ham_tham_so.vi": "6696caa06625715f6bc25753d89c0aae2a2307fb3ed53f53354cb75878aba6a7",
    "scripts/test/kiem_tra_ke_thua.vi": "69c8fe3b05d839a3b9e97f011f06c80961c16e4cda8091d85c0b411c6937510e",
    "scripts/test/kiem_tra_khi.vi": "1c932c5bbcf0c12e4398931f085733ecf421743afe484438a464c561e8e72dff",
    "scripts/test/kiem_tra_lambda_hof_mac_dinh.vi": "cd1885390d310cc61fa88088bc129b862bcea1e4bdbde30121015925dc36def7",
    "scripts/test/kiem_tra_lop_phuong_thuc_forward.vi": "e371a0ffdc8a9295b8c1287a7b8f7c1df446ce43b60d39ea2e7f52a34febe709",
    "scripts/test/kiem_tra_lop_truy_cap.vi": "f9a61ebf04519613f84326b324a7efd936fbd3de9158521109ee59eae0728014",
    "scripts/test/kiem_tra_mang_3_chieu.vi": "0ca07b918cbf23df10dbb233d5c578fc88cfa291b47f5c80e0b847ceef68375e",
    "scripts/test/kiem_tra_ngoai_le.vi": "dafef5c02f32cc4bf27a0f81b5404cd7f9b7f1cc5f8f7b69f3282a19b6b9716c",
    "scripts/test/kiem_tra_ngoai_le_xuyen_ham.vi": "6ea614d1466eda57d0476016a5357e80f21d5489eacd3e902075b0ae380d7507",
    "scripts/test/kiem_tra_nhanh_dieu_kien_moi.vi": "c5ef70405fa06cd2d5cdc2206702362bc93016cf84b07cef003d1eb45dbe6fb3",
    "scripts/test/kiem_tra_noi_chuoi.vi": "021e3eb52e22480a8a2b164510dfcbe3e791aaecef8b537c3c7d6438cc6890c3",
    "scripts/test/kiem_tra_rc_hardening.vi": "83c36274eb8988f61586ed1e0f5cd1118e085b5b4621a63c683c56e5f1253803",
    "scripts/test/kiem_tra_object_model.vi": "44d624d33c3e520dcdc395cdcd666f3324657ee9f013e789b2eaab40be69b928",
    "scripts/test/kiem_tra_receiver_minh.vi": "59a36446aeb4e501842abe5cf26bbcec0362e53a3f708842d95c8137a9340b71",
    "scripts/test/kiem_tra_receiver_minh_de_quy.vi": "d893de78e539653eb33de8f55950c98fe57553c01cf7e27fa51b7c0bfe5759f3",
    "scripts/test/kiem_tra_receiver_minh_dieu_khien.vi": "5ab679710fc327f82eb8689a1a43295d3f62b18ac591c52a53f3cc9ea0572db9",
    "scripts/test/kiem_tra_receiver_minh_nhieu_instance.vi": "0936c7ba0fb33b3abd3326e489acfb834f7d40adce2ef4fe7f945777958a94fe",
    "scripts/test/kiem_tra_semantics_gia_tri.vi": "9a833f150af890fd7decb034b9b361aa60bcf8d5a5947fa34a034340180ace55",
    "scripts/test/kiem_tra_so_chan_1-20.vi": "fb666c3b8188da3b332ccf1a3a225cdfda3695a0ef30bde02045c89aec3a6fb9",
    "scripts/test/kiem_tra_so_chia_het_cho_3_va_4.vi": "f7e0ccd6792c00f1a99c8f215fd28fd28e6ecf20f43bd356e5e734968401d788",
    "scripts/test/kiem_tra_so_le_chia_het_cho_5.vi": "d36aa88216b203d79dda12ad7cb0baf7584e9b1b5d245946cfadc0b3083c74d3",
    "scripts/test/kiem_tra_so_nguyen.vi": "3ee00268ab56251ee1c936a6c8de67cde7229617f309574399fe0646b8fee5f6",
    "scripts/test/kiem_tra_so_thuc.vi": "27e9f6a10d806daebae1c1115762b313ca713ae1e77492b3530d677064952614",
    "scripts/test/kiem_tra_ten_kieu_nhieu_tu.vi": "553624ddc6d649ef52f995ad0ddbee4251a3bfb5d0d999f4a5c43ddcc70f3efc",
    "scripts/test/kiem_tra_thoat_vong_lap.vi": "6b53b3989a5ea7c11a9aba2ca60344abc3ff5ea8dd76cd04e5f6d4aa5bc496e9",
    "scripts/test/kiem_tra_toan_tu_bit.vi": "c5910101002f04846fb80eefa56175af1561f1655eee7d75bf43111d7198a407",
    "scripts/test/kiem_tra_toan_tu_moi.vi": "2b0d1eb5096349f702ca873a6312677e5219853ffee42182c987e138f52c2460",
    "scripts/test/kiem_tra_visibility_instance.vi": "ba0b881598550c5c376548ea77c03894abb9fd6342acf73173abde476e79efee",
    "scripts/test/kiem_tra_tra_ve.vi": "026f5240ba27e10e58dfe326548dfa872908849c905a92b318fc10adf4faf0b5",
    "scripts/test/kiem_tra_uu_tien_diem_vao.vi": "ad15518d95d36197a4eb4c678ff61266418561d617c8764993918401adfa4557",
    "scripts/test/kiemtramacdinh.vi": "51d36cc9d5314caa663a59ce376b57a483165f37b12903388c8ac34de986af6d",
    "scripts/test/kiemtrathamsotrung.vi": "0585242713804be60268a0c8124f251db41f0c51e58a01b8b8444ee232ea6cbe",
    "scripts/test/program.vi": "b8308e5b12f4497c1c967aa9934393d6a04129a0c56c68bf2fb7908d66085c4f",
    'scripts/test/kiem_tra_aot_tang.vi': "525a467eafb028d31eb4c059a213b3da38360da90cafb6606fef6331cc0d1063",
    'scripts/test/kiem_tra_embedded_vm.vi': "28eabf0abf6d463b62467a5896279ec5b52fa59c361b28ccd4928c62365bbc0b",
    'scripts/test/kiem_tra_diem_vao_ke_thua.vi': "1ffbf661a3860e22f199c0369892ad4cb8786198798fa14b0ac946b65dac18ed",
    'scripts/test/kiem_tra_diem_vao_lop_chinh.vi': "ade1d784a0baa4864b17e60393e8e3e6e2d96a5a8b5e4fd896242b6e0fb10236",
    'scripts/test/fixtures/entrypoint_inheritance/defaults.vi': "fe976367ca0d87b567c127f95e4ac7ce9d85135ba12da9564003a148d9f05cc5",
    'scripts/test/kiem_tra_jit_mvp.vi': "2537d48f074358ba1db921565bd0cf6f84bd1c0bdc9231072c1e36cebce67981",
}

# Golden-verified sources that resolve library imports from VPP_HOME. Each has
# run in an empty working directory with external assembler and linker denied.
REVIEWED_IMPORTED_SOURCES = {
    "scripts/test/fixtures/native_aot_facade/entry.vi": "2367346bd74bc64569e8152a4824af32bc1becc14d71fdba487669de471bb693",
    "scripts/test/fixtures/native_source_paths.vi": "8814c763635aa7ba31063398b34a194da546b3fe4403b6970fc44a152cc6b7e1",
    "scripts/test/kiem_tra_native_leaf_vi.vi": "a2c2359135d0fd0fc7fd0bf1b06ce0f61d97436b01c8d0767ff4aa3126350968",
    "scripts/test/kiem_tra_loi_tep_co_cau_truc.vi": "3a4929caf7e6edcc873026b616bede9dc910249be93a0ee1bee905f7ebbcf7c5",
    "scripts/test/kiem_tra_escape_bien_dich_tu_chu.vi": "a0de862f851d230ff14b7e799881574689cd443e425fec2a592c78662b155910",
    "scripts/test/kiem_tra_kien_truc_oop_compiler.vi": "4c2bcd0fe459938c9f5aad654a0e2456b16dab6504be8b6c6641d23b6e9d4861",
    "scripts/test/kiem_tra_bien_dich_tu_vung_vi.vi": "77acb1435c5551d380b49993d8065faae4c8656c2b0845c935b6379a6a623bec",
    "scripts/test/kiem_tra_tap_hop_bien_dich_tu_chu.vi": "b06c664e9d995bdba086a672e6f3dcedab647179991f4b50eede531249b35caf",
    "scripts/test/kiem_tra_quy_trinh_oop.vi": "816940b0f83959300c7924010951cc15603143decd00db01432e5c8c5652018a",
    "scripts/test/kiem_tra_lexer_token_gon.vi": "f1b9d89d1e81e14c5585f51b2dcf52a00626ef0f20a9596a27df0d2b90ec336e",
    "scripts/test/kiem_tra_lien_ket_tu_chu.vi": "370a3f9b5e1809591e28427e80cd3d4d9a7cc504cb4bc18aa72f5b0e1736c258",
    "scripts/test/kiem_tra_so_ham_module_tu_chu.vi": "73a1cba0b8cadec686f9f0f23f9f54cbeb48a8dcce727cc8e36c000902008101",
    "scripts/test/kiem_tra_khai_bao_kieu_ffi_tu_chu.vi": "b0b6b6f5930e9665bbc2e0fabdcd0f5d1546cc2025cb0df0283f22281e15f76d",
    "scripts/test/kiem_tra_do_thi_module_tu_chu.vi": "eb045353b43d5d1e9efc659cd6a1a1c9faa941fcd6dad94a0048a7d4ecb48a41",
    "scripts/test/kiem_tra_goi_quan_ly_goi.vi": "123ac9a517723564ccb60bb4aad2304c51b500e00e78ace73bd66cedbba3bfde",
    "scripts/test/kiem_tra_native_oop_snapshot.vi": "c6938b9515170cc31c631f7a5ecfaa31d90bb936e0e18723876a267d2595b77a",
    "scripts/test/kiem_tra_codec_uuid_bitset.vi": "cd233e0553206ebcf3f44b03e790398a514b6571deeef5615429e7ad9c4ecb40",
    "scripts/test/kiem_tra_dns_cau_hinh.vi": "96df4eb380d46ca93349bcce03f3d11d2e59f5a610124cb8c34d4d8c26e7fa92",
    "scripts/test/kiem_tra_ham_an_danh_tu_chu.vi": "8d02c8dcf4d0c3b5b33eec4931598c16a9aa654bc62a83692b18d3143f5c2bd2",
    "scripts/test/kiem_tra_ffi_gio_dia_phuong.vi": "4dbe6550b7342d11d2a51bf06cf77503eda477c43ef26a74c60a368ccbd1f251",
    "scripts/test/kiem_tra_lien_ket_module_tu_chu.vi": "8afbd1ebbe05d76914248888fc087857f7e042f8bd0eb8cbd59e03b2fd2aa1c0",
    "scripts/test/kiem_tra_native_oop.vi": "0ae79a953a082b9910c8d98f7d4f29f340e5f65e5a7ff56f1cbbe7d99e2f0d3f",
    "scripts/test/kiem_tra_aot_byte_scan_set.vi": "0fbfaf81e64e9d590967002bb6b2c450054dd607076ff80c74d4efbd95bf6ef6",
    "scripts/test/kiem_tra_aot_i32_run.vi": "2d054ada85f4befe2f9addb393b7905df962889000e090b07317ae461d5fd20a",
    "scripts/test/kiem_tra_aot_index_offset.vi": "54cdf39a7dd855253f61b6861e97f1c9fd90608228dbf15198d0c4fd2ee196a8",
    "scripts/test/kiem_tra_aot_map_remove_value.vi": "e6d7bf9ee4a8034aa44c4c465b8110528be18317e90e484cc9216ec210276b66",
    "scripts/test/kiem_tra_chuoi_co_ban.vi": "00ae0c727a2c3e725ec804389ad57dcd36f7f6661e62f305544bfebc5bdf83cb",
    "scripts/test/kiem_tra_gia_tri.vi": "d96197b1c8a127e61c53b3ceefd0154db3c8a6a0a24340040cfac2655a3b1871",
    "scripts/test/kiem_tra_goi_dung.vi": "f888e804dc5d7a0cc0e9d7c0612a3a2920919577f590d467a9c49f9ca908742c",
    "scripts/test/kiem_tra_goi_ham.vi": "2ceba673bc69765844f9623f5fe3e128a0817b18982eed9aed2c65d0b438e04a",
    "scripts/test/kiem_tra_goi_van_ban.vi": "fdbce05d0287f3aeafe8b63301184be8ef64abace1f9c2db7becec4e14c2a1d9",
    "scripts/test/kiem_tra_goi_cau_hinh.vi": "8a8eb32176c9ef7dca697087de741be4c76a5122043d6a3fedb10ca390b2b1f3",
    "scripts/test/kiem_tra_goi_tuy_chon.vi": "241a998f2e0845d49192764526a19ecd7dd9c392b5bb9f8b9530531e37475ddd",
    "scripts/test/kiem_tra_ha_tang_oop.vi": "47793cfd8570362bf72197e1ba8ba80b632cb6b0b0f7f6769e89d87881a2fa21",
    "scripts/test/kiem_tra_json_an_toan.vi": "6c46be9556ac64f37d08c66bec80d122b3282fe7430312f250782bf8014d196e",
    "scripts/test/kiem_tra_goi_thoi_gian.vi": "6610806247d1f4ddc01a1b22693a893e1d479e5e8b71e5ee6af6c29c50cb1603",
    "scripts/test/kiem_tra_tien_ich_oop.vi": "a07c0e30370176386ee6425a908f96a94a695111a14077a3dc375477d312ad26",
    "scripts/test/kiem_tra_tu_vung_oop.vi": "ef3e55f219a2c68cc3252e105350c0e96fbb1398b1b5219f99528457bb6906af",
    "scripts/test/kiem_tra_stdlib_crypto.vi": "c39bd7f72e7869424f241ccf1ebb2cbd7c6e9d7c682091b6d94d6d2f8c065e9b",
    "scripts/test/kiem_tra_goi_json.vi": "60d99702540fb99031ec4b6e2639b6f02466bbe8b908357094a555d85c9a13da",
    "scripts/test/kiem_tra_bo_dang_ky_ham.vi": "33a9792d25d68f7d96cf36b8a8e8b7d60caa38550013d01a0b71597fa909c0e6",
    "scripts/test/kiem_tra_du_lieu_oop.vi": "6b23b18461f3f1f60ab3d40ba580b8df9654fc857d5ba668021a399847cb7bd0",
    "scripts/test/kiem_tra_luong_tu_qiskit.vi": "16098878d89cda2cd1c79081eb511e4de2c1909556bc0a4b0e71996693b7168a",
    "scripts/test/kiem_tra_rest_json_jwt.vi": "1cdfe0f84b9dd9aac61063da47486a13c467eb0855f6d557ed91c9e4ced492a7",
    "scripts/test/kiem_tra_tuong_thich_oop_toan_thu_vien.vi": "4c242eb219723abb88ac39fbe449d856bfcd84dd954825752fd00fc68ef2611c",
    "scripts/test/kiem_tra_opcode_chon_ca.vi": "b0eb7289434fabdf5547d01120bec03cac68ddc3b700b7620293d3c5196040f6",
    "scripts/test/kiem_tra_goi_mang.vi": "5fcba36d27998643fd8c261911158c029a7060cb33298bd955b245e5bab45958",
    "scripts/test/kiem_tra_rong_va_map.vi": "ea89cf2f0200169c811d67954050fe25742116c3ecd7610f642669b86fbab89f",
    "scripts/test/kiem_tra_van_ban_oop.vi": "d16463c96ed79f925210d9e66cea3914f691a91d5b36a3f22c941b09687d0b8c",
    "scripts/test/kiem_tra_bo_noi_chuoi.vi": "69b2dcbdcd212896ff1e7117d709ad7661dae08aec1eab8960c7fbe83e48d583",
    "scripts/test/kiem_tra_stdlib.vi": "c4da1b9ab601e0fa6fa87c2b1fe4bbef6ba1af0d75276c74e85bdcad2523df9d",
    "scripts/test/kiem_tra_stdlib_tinh_toan.vi": "1d3a8ff078135697549c90353e817fbbf9327ce6bd8045a641bd269b56bf867b",
    "scripts/test/kiem_tra_stdlib_mo_rong.vi": "cff4c2c4ed8f47b43189eaee04803f37105d81f2d6c134143526b57f92a1b63b",
    "scripts/test/kiem_tra_bien_dich_bieu_thuc_vi.vi": "5846b8b9d1beafb0519fdb17794e46043e7d26cf55cc8f73b09d920cca1d608a",
    "scripts/test/kiem_tra_goi_mat_ma.vi": "3d445582d9d84ea3663d28c169fe12ce3724f68e97c7d51537d1e23cd85b7519",
    "scripts/test/kiem_tra_json_dieu_khien.vi": "dd336ad8035b13f70a09ce49b12c22a44b78094b660245c9230c842d7a1d609f",
    "scripts/test/kiem_tra_json_integer_overflow_scan.vi": "42776c0ded2068f1eac9cb2c0f455f81ecc361d8f1569ff4e111723bc0025ed9",
    "scripts/test/kiem_tra_duong_dan_windows.vi": "27f22d8294569b580e68c79b0d1ddd0c51a0f6bf5ce6b0fa8147f08ac82e9c6b",
    "scripts/test/kiem_tra_goi_ban_dia_hoa.vi": "7b6eda73ec9027493c1eb60ceeae956f76b5a9ed0032e440501ea226b182cac5",
    "scripts/test/kiem_tra_json_escape_byte_view.vi": "c7cf16808f1fa030c1525b97428f12be1e598fc3dd81ddd6cb2bd32fba49ef32",
    "scripts/test/kiem_tra_json_key_scan.vi": "1608e5645a759e56bcac965417fd8672856f540fda3c6d058f729ecd0ae21c6f",
    "scripts/test/kiem_tra_json_utf8.vi": "ca7307bd898a721dc390e434f55e642301da6316ee81d4e164980023549deaa8",
    "scripts/test/kiem_tra_thoi_gian_lich.vi": "7ef070a8e42d3506d728ec78a4f55108c67dd0d11734dbbf749eb6679f5510d6",
    "scripts/test/import_main.vi": "c76fbd85b7c645a2d8537876761c4ce33ec43708e8c1f1aae2fa01bcb394f177",
    "scripts/test/fixtures/anonymous_modules/entry.vi": "9cdce8babd05c97596abd533d94ed5d901c8875250ae9771adaa2fa231669ef1",
    "scripts/test/kiem_tra_database_oop.vi": "f68e1dce725c1e042da953ec0d52aea4512908f79d3efc43c20117bd81248194",
    "scripts/test/kiem_tra_collection_bai_63_75.vi": "69e781ceac6b22b46bb6b8c1c4247f6bc76ddabc0c010868492b0ddd3647f8e7",
    "scripts/test/kiem_tra_collections_oop.vi": "2e362382894c30bd4f8ffab67f3d462d9faad7cb4cf59c5885ba7e33a549cb38",
    "scripts/test/kiem_tra_doc_chi_so_ket_qua_goi.vi": "2197c2a4fe9e6876d5df08cf4aa63ee369c052d04504017b1a9ceea388da326d",
    "scripts/test/kiem_tra_goi_bo_suu_tap.vi": "e126b971ef938f8ac900d5cff333944db786f925733bc7ad1caa9ba17e8ac976",
    "scripts/test/kiem_tra_goi_bang_ma.vi": "edafc62db5e9d30ee723536ccba302d88eab2ae2484b8f3058f1ba556a85ea7c",
    "scripts/test/kiem_tra_goi_db_config.vi": "f26f81a50e55fb6958d9c2d0c6e154a761a5604840a7245088683a5f5c6bf080",
    "scripts/test/kiem_tra_goi_ngau_nhien.vi": "c5a849cb11580175697cb83670f2600ee095a7f269d1eb8a4f8daba1237ccb72",
    "scripts/test/kiem_tra_goi_nhat_ky_moi.vi": "db60aba339c5dd22e219e9d41bf11c84738ce7ebb04018c1132cb057d88c9e6a",
    "scripts/test/kiem_tra_goi_hoa_hoc.vi": "e89268b81f4299ff6b5bf36c4273b0e1b049fd8b333ba3d63f68f6f0d9085b74",
    "scripts/test/kiem_tra_goi_kiem_thu.vi": "9f2cacdc6209287a5ca60fe84f5d27c41bba83a1d15ea68da0be36415d3199bd",
    "scripts/test/kiem_tra_goi_regex.vi": "7d8f1ed386c229be3beee9a188b0fb60eff1a043c01b759132b5733d0be61e3b",
    "scripts/test/kiem_tra_goi_tap_ban_ghi.vi": "d349a2d5bf75784237452c78a4f40daa1275563f97fd6fb4fc11a00b851d61c1",
    "scripts/test/kiem_tra_goi_toan_hoc.vi": "461dccf422324cee42fa39c0b890c263208c861006f2748046fbb63a127cd88c",
    "scripts/test/kiem_tra_goi_vat_ly.vi": "d3d9bddb9e3c3ea90b591223748dbaffa580d1f9967afd4745a1d7bafdaba1e2",
    "scripts/test/kiem_tra_goi_xml.vi": "039454795a8cddd6fc71f71c478f7262e44b6c4a8022a86531321a2c8d482047",
    "scripts/test/kiem_tra_khung_bien_module.vi": "0f40a4ccadee9e4508f34092e05ca927cf29d21c0ac10632e19e93baa97b336a",
    "scripts/test/kiem_tra_luong_tu_openqasm.vi": "d490e1c69831d96290966348613dd8c5903c343a5fad73becdc49c36f66e0dea",
    "scripts/test/kiem_tra_module_alias_lifecycle.vi": "2eb0ad5c66abecf7ea6adfa2a596c3602e4594e6b40499c3ee75b8ae25e695ae",
    "scripts/test/kiem_tra_module_alias_trung.vi": "28fa7deaadbc58f39d958fcdfea8ee4ec56e7ce06b6b72892a5c88d3181aecf2",
    "scripts/test/kiem_tra_module_khoi_tao_mot_lan.vi": "660984432accfbc94dcdfee8d6af3ddf1eb7b7cfc429b9fd15c1f6c55b1dba49",
    "scripts/test/kiem_tra_module_lifecycle.vi": "416110fea7300a01012de78be5805fdc69cc4f0a25ce17a3cec70d8de30ef0c8",
    "scripts/test/kiem_tra_module_reexport.vi": "ba9489ca0ee6cb22b8be89c079e84771973e5f13c951633bdd3736fb67960751",
    "scripts/test/kiem_tra_ngan_xep_oop.vi": "3504536c5184eefcb34ffc520a6934e48d7338d18599e202670a4f76d9468621",
    "scripts/test/kiem_tra_namespace_module.vi": "43b1e2cdc681959120595641589b9530db75dd1673d8324e9275f1e8f9bdc01f",
    "scripts/test/kiem_tra_package_09.vi": "2dcc4409f99d54238b7f4713137ed98e88f75f4a45ddca065781d9fab0f33328",
    "scripts/test/kiem_tra_ung_dung_oop.vi": "ac3811a21f32252d7f1fce065b8ce319afdb18e6ab4b64a3ad6b8eb2770f9cd3",
    "scripts/test/kiểm tra tên tiếng Việt cách từ.vi": "290ddc35a93bc71b9460fd3631f5714f35cb760c6eec80a460466996a1f5bbfd",
    "scripts/test/kiem_tra_ffi_thu_muc.vi": "c63a9de4305c185916776897ec55ee76037fc7dafeeb02ef918ea69eb7f6b58a",
    "scripts/test/kiem_tra_ffi_utc.vi": "f396b0a05c080a3c5859eef3fa9260d538bbf209c2edaaa97a17ca3d603a8b5c",
    "scripts/test/kiem_tra_ffi_moi_truong.vi": "9f37086403a4da75f3067f2d9cfa8d8a309e538cff19ea091e64668c8abfe68d",
    "scripts/test/kiem_tra_ffi_don_dieu.vi": "5a1beccac4c53d07983ef9ddd5932e6d4b4a605cd6e3fda54f911b3466534342",
    "scripts/test/kiem_tra_goi_thoi_gian_1_0.vi": "5a69a5c5cf3718840bd1f866da7d67b06c85de86fd57e8ef35610b8877c01e90",
    "scripts/test/kiem_tra_package_modules.vi": "fd98d2ccc99733a23fa4c048449f906debfa0a12b6bcad15cc295795ff780c6a",
    "scripts/test/kiem_tra_goi_xml_dom.vi": "760acd9427a7a20dba94433e38dbcbd1c7b4dcfa9da788571b7b9c996ddaa715",
    "scripts/test/kiem_tra_ffi_entropy.vi": "505d5154938a810aee2509866e1c86ccfee8e159951374dde9d74cfefb6bba45",
    "scripts/test/kiem_tra_goi_nhat_ky_1_0.vi": "966de275dee4b19db7896dd3546e21912a6fe6aad17abc46b0bd3c4e8f99ee09",
    "scripts/test/fixtures/native_path_kind.vi": "7b5dc2d847b59fe6984fd1a19fee36d66cc2c1955107ddd0ebe6ebba6b1043b8",
    "scripts/test/kiem_tra_file_dem.vi": "95c0212bf27edffb1dcdc3ce8ad17e7df00f16fc1dfefff3cefe26a9639e5ef4",
    'scripts/test/kiem_tra_aot_string_intrinsics.vi': "86e7c90980fb5e53503e7bac606126c2f892d843dbc57f70793f9770b58261a5",
    'scripts/test/kiem_tra_aot_byte_scan.vi': "f98e8258927c7f04261352a343ea2f1f7a25c80b20ff3803418e8b2f7c518f84",
    'scripts/test/kiem_tra_application_server.vi': "6c050da4a542063ffa23c6d01eb942f3be7c16c82eeb7f7800f96868d3864ddb",
    'scripts/test/kiem_tra_aot_byte_scan_heap.vi': "934516be09ab538e23b969fc67bc8c4c26b5627906a954087f6ae9457a112df0",
    'scripts/test/kiem_tra_bien_dich_khi.vi': "242cf1f7d17514a57a75681f55bda9cd9c5eb19a52614454cecc2e2b0a14cb52",
    'scripts/test/kiem_tra_ffi_local_windows.vi': "710cb2caed5a22b4f00156afb4da53002700ca1d00b1470468c31d2fdcd5e05d",
    'scripts/test/kiem_tra_ffi_socket_loi.vi': "517412ee7e0ff9e3f7895ae9cd8bbfc251288bc240fee405d3542dd5f4e46998",
    'scripts/test/kiem_tra_dich_vu.vi': "81475f539d0f54f0c18bdd410ca16799579fb9c0a10e3db62e905fa1ddd8abbd",
    'scripts/test/kiem_tra_aot_type_branch_edge.vi': "98c8016bfc8d7d7c669d0058be93f0d5f7b1a11c47ca4d3b506637a8bd2f7b4d",
    'scripts/test/kiem_tra_ffi_tls.vi': "1e46c450ed52d9bdfd7d24b3a07f43164e63c2f8b7eb92deeea9ec9e0341515a",
    'scripts/test/kiem_tra_ffi_thu_muc_windows.vi': "9562869536145680745896c34ef5a89dd0f7aecc11a312a4dd9de7a7fd952486",
    'scripts/test/kiem_tra_bien_dich_khi_vm.vi': "b447e9b6646c40fbae5a65f2182dc4acf65e10ac65d25dcca2bf3561b2d1f893",
    'scripts/test/kiem_tra_ffi_http_transport_bat_loi.vi': "fb160a9cb54cfdb57799729562cce77f0e7fe2f9fc86f3e2bf7122a761fa6074",
    'scripts/test/kiem_tra_file_stream_oop.vi': "08ec1926d20d6e2de59ba5767eefcebd7328e2c1911303f90382612f34e584af",
    'scripts/test/kiem_tra_gc_mvp.vi': "5877905d6dfc8f7ebfa2ea80b106e72226d93f7b0532c99062f6013a0487170d",
    'scripts/test/kiem_tra_goi_bien_dich.vi': "35b81da6c5840a686e719ae9050b988004c146235829f50cb01d98411f2dce24",
    'scripts/test/kiem_tra_goi_kich_ban.vi': "7d667d32c616787dd18260347e510b86f45bc86d873e317a5351069b9b7dc652",
    'scripts/test/kiem_tra_goi_cong_cu_luu_tru.vi': "a030106b89af1d7ee0539575f9d73090a9986f6c47b010c8185950fe89e3a06f",
    'scripts/test/kiem_tra_goi_may_chu_http.vi': "a08980c25067985bc64641c8e940ef219ce98b578b893339059a1472e747a72f",
    'scripts/test/kiem_tra_goi_luu_tru.vi': "2d67ffd3753d54002f57592c3b1d319b4d6008cbe2f34d1f946c41802eb4aa14",
    'scripts/test/kiem_tra_goi_repl.vi': "84f5ab717af0b45cf1a389b5fa6feb8b5c2de0b186af737e04bc795a254cde26",
    'scripts/test/kiem_tra_goi_soan_thao_tuong_tac.vi': "30bcffaa3d45ccc59fd3da46fc83c389f7447aeed1f1a251320a4bc8076dfb6b",
    'scripts/test/kiem_tra_http_may_chu_thuan_vpp.vi': "5b69aea7c39910a8555e29680d44d17d566a4b89180614737a2b6ff0aee6c620",
    'scripts/test/kiem_tra_http_client_oop.vi': "6c82a11d1bfbd2992eb853b7445cb442096f284ad7a0b4da43d634f9cfee5d23",
    'scripts/test/kiem_tra_http_thuan_vpp.vi': "e79c7a1122cac08091992599a4cb00e2c8abe74ab4765e7ea49c08f1229998b7",
    'scripts/test/kiem_tra_goi_zipfs.vi': "6a7ec92f749be8115c7809dd78da8728607169d558e462104ca142b69a714bdd",
    'scripts/test/kiem_tra_http_transport_bat_loi.vi': "3545aa3d34a4474bbea452eff77de96aeaa64c0d9d75127662a22d9e690030c8",
    'scripts/test/kiem_tra_json_byte_view.vi': "412220c3c866ac9f7f29be36d9ad0b5ef24f08e0fa87126294e6b61cd665a818",
    'scripts/test/kiem_tra_json_file_roundtrip.vi': "13a38fccdbefd1a2471ce24971bbfee5c01464de78add0724a16a23e3d02acae",
    'scripts/test/kiem_tra_json_mixed_i32_run.vi': "b5e38242e968a6f1c277d0664c9eea99ef26d17f60ef08188f9f7c9bc09bc2cf",
    'scripts/test/kiem_tra_json_member_lookup.vi': "ba33711c25b40b42385a42254d5ee44db8e5a4a1208234ee78c4361e23c9e534",
    'scripts/test/kiem_tra_kien_truc_oop_compiler.vi': "4c2bcd0fe459938c9f5aad654a0e2456b16dab6504be8b6c6641d23b6e9d4861",
    'scripts/test/kiem_tra_logger_oop.vi': "6007fe4f5e2e7e84430fd356184c80c6f56f6837b6732dc9d051f5c604b753e8",
    "scripts/test/kiem_tra_nhat_ky.vi": "1ab4c39e3622b2eebb8eca625d9d4bc10c127537200b7a46dc99b22f569e7af5",
    "scripts/test/kiem_tra_lexer_token_gon.vi": "f1b9d89d1e81e14c5585f51b2dcf52a00626ef0f20a9596a27df0d2b90ec336e",
    "scripts/test/kiem_tra_stdlib_starter.vi": "4b3478100b0c01f6299a81e5abecca9776808df3ac5eb479f242db4f5682956c",
    "scripts/test/kiem_tra_stdlib_nen_tang.vi": "c262d9d8e7184b728f2537d91ce87ddba4c36566ef03d01aaf70a5f4f15ab2ee",
    "scripts/test/kiem_tra_tong_hop_khong_xung_dot.vi": "cdfe6fb216a03431c2cdfdb7a478c55d7e5147c9313d3aed12b8b4acb71ee8b9",
    "scripts/test/kiem_tra_su_kien.vi": "a181a580e1c0b9b603c0915dc505b0bb056b958d05ee4b0cb9b83eef9e273856",
    "scripts/test/kiem_tra_quet_tach_chuoi.vi": "8b25cd70f392b3cb0cd604050a5897981ecaface130027dea58509b315c4f17b",
    "scripts/test/kiem_tra_api_db_project.vi": "e7ea8f83a6e2aa552a2660d820e9d85f10d764aa5590780127b6a2c9a1eddcbf",
    "scripts/test/kiem_tra_tuy_chon_thong_ke.vi": "cb0bc32cb660208da4c6dac82578f482ec95c6723f85be9652754849c5c489b6",
    "scripts/test/kiem_tra_stdlib_io_config_time.vi": "aa4a2ece517cf535f6e8e9da787a5820fd1200b479f02ffcfbd935602f118e32",
    "scripts/test/kiem_tra_bien_dich_chuong_trinh_vi.vi": "85184f78f5b57caf720c5a08d1761c930577f8e5ef5d2f44d2d4b6e021bb7dd2",
    "scripts/test/kiem_tra_aot_replace_single_pass.vi": "8e5c8260ec7c30dd829de8224a10c35ad936b4e1f6cfa7d1dfaa4c1b5a7e815b",
    "scripts/test/kiem_tra_dns_cache.vi": "ff78808f475433bef87a6a70bd938bae1bb119b6fdd76545294c0e4a5ba3267f",
    "scripts/test/kiem_tra_dinh_dang.vi": "aacafea37f02ed39a5a2b6440d05680f3dcf0ca2e7a041f3e94e04e66e46290b",
    "scripts/test/kiem_tra_dns_goi_tin.vi": "6652f80eb32bc26a8ac6ad8bef844039fb5729ff84c7c65e36810612c62355dd",
    "scripts/test/kiem_tra_dns_search.vi": "f3101465c6bcfb0e25f68ce974a30bab31ba53d0b205561272c08b82dbeaed5d",
    "scripts/test/kiem_tra_http_response_vpp.vi": "b9a9456ad6ae1cf98917d888475dad4e2f611c8d8844c036413b6f08bc84effe",
    "scripts/test/kiem_tra_luong_du_lieu.vi": "639fd845ce90dc4d9fbe13d6880f76bf0a53a4781111ce1a9e927594818dcefa",
    "scripts/test/kiem_tra_quoc_te_hoa.vi": "1a8d48fdddb5d273ec8d99f076c8c2aa7c3f8f6c74a5af552a99f69068e63624",
    "scripts/test/kiem_tra_vm_ieee754.vi": "bafb028c08275587029673c7a4e391770febf9667ff45383dfb98ce12c8d6daa",
    "scripts/test/kiem_tra_package_tieng_viet.vi": "bb8aba18bbb9d08465cba91bdf72bc371aea2e9682b43022a56914afbdb1aa2a",
    "scripts/test/kiem_tra_ast_oop.vi": "ec7155c96926c06db800326e3df16ee5b4cf79c4f478291ecdf0d097dea738cd",
    "scripts/test/kiem_tra_chuoi_nen_tang.vi": "a4e38f456ea6b211900f4977f74b70787a7667ae03e3ebf6d831a14fcd64553a",
    "scripts/test/kiem_tra_bien_dich_tu_vung_vi.vi": "77acb1435c5551d380b49993d8065faae4c8656c2b0845c935b6379a6a623bec",
    "scripts/test/kiem_tra_chi_muc_ast_bo_kiem_thu.vi": "399d497aac8ac1edf8628660decad3f77a52a54b2f93aba25b785ffd88a96137",
    "scripts/test/kiem_tra_oop_bytecode_tu_chu.vi": "b0a2181d1fb44ccb781c9bf8b50705320d194fc86567a1bdf27e434dbbf4bc2b",
    "scripts/test/kiem_tra_quy_trinh_oop.vi": "816940b0f83959300c7924010951cc15603143decd00db01432e5c8c5652018a",
    "scripts/test/kiem_tra_tap_hop_bien_dich_tu_chu.vi": "b06c664e9d995bdba086a672e6f3dcedab647179991f4b50eede531249b35caf",
    "scripts/test/kiem_tra_aot_type_branch_missing.vi": "ad764a87eb72f89af36b9a46b44f9e344e31b869260c9785174b2ad6505ef794",
    "scripts/test/kiem_tra_aot_decimal_long_source.vi": "f98b37b611eb28facf8272b0ebbeaeb905520e43f4314db3019eccf671e64109",
    "scripts/test/kiem_tra_json_escape_span.vi": "a4e44d788b8a8c0feb156aee255f92f77368c243c5b985f28cd7b718298c2457",
    "scripts/test/kiem_tra_gan_ket_hop.vi": "af58ddfc965ab55d3dcdfda4759a7fdbef2b41cb791cb34dae836f40d9b2ec2c",
    "scripts/test/kiem_tra_lexer_bang_byte.vi": "d0e223cca00e309a9829b5d6030bb40d25fd16118b6cd0ec1db492c6f32cab3a",
    "scripts/test/kiem_tra_sort_early_exit.vi": "7f75c831d9ec70bbc536da10fae64abc5f86a6153278c6c329dc9be804bd8876",
    "scripts/test/fixtures/entrypoint_inheritance/imported.vi": "64590d103a4d2aaa1a16d37605ebb6901efe6ff930d1486db173a27f6beeb5ce",
    "scripts/test/kiem_tra_api_thuc_thu.vi": "64e33747945e9086ade94e63ee3eb2988f3b370d2e598ef483f0db850321eaa2",
    "scripts/test/kiem_tra_phien_bien_dich_oop.vi": "e9688f59f8e9ba1070fa9464b7a72851f7899fcebab1695a3d473fc6e0dbf30f",
    "scripts/test/kiem_tra_ffi_tien_trinh.vi": "751d09541df23538fa53efefc33f2e92b694648fc10f6266379d5f7586fec8c3",
    "scripts/test/kiem_tra_dns_transport_day_du.vi": "914422823112d2619ddb5865ae89c2191182670d2ef527660f9199f560328fc5",
    "scripts/test/kiem_tra_socket_ip.vi": "c6f61ffe2f8217aae194f6826293f266bfa8948deba60019ca32be41424fdaa9",
    "scripts/test/kiem_tra_process_quote_windows.vi": "088f14966270d1b94d0f608fbe823d41a80b9c81cd6c9608c95c640ff10db5f5",
    "scripts/test/kiem_tra_o_cam_tcp_oop.vi": "ed33a2b61d8c6b02c0597497a0f81fa440c642572383f300d966853aeffadab0",
}

# Local source dependencies have to be pinned as well as their entry source.
# A changed parent module routes its entrypoint fixture back to JIT.
REVIEWED_SOURCE_DEPENDENCIES = {
    "kiem_tra_diem_vao_ke_thua_module": {
        "scripts/test/fixtures/entrypoint_inheritance/parent.vi": "797959c3cbf4e1dca607915f32ee346d82d4af00f203488457bd28fe7750163b",
    },
}

# These source programs read fixed regression fixtures relative to their working
# directory. Only the exact hash-pinned inputs are copied for AST execution.
REVIEWED_RELATIVE_FIXTURES = {
    "kiem_tra_native_facade": {
        "fixtures/native_aot_facade/gói/demo/chuyển tiếp.vi": "3098de37a8c32a402e08649b2975d54544084339f4069b06e92a2ec65c915131",
        "fixtures/native_aot_facade/gói/demo/chính.vi": "93f16c3b361fac07000bb73cd131dcabffd86499bb3f6b00b4c8186e934ed4e3",
        "fixtures/native_aot_facade/gói/demo/phép cộng.vi": "344b875767ba85e97232e29f5632f6a94fe5b2790110565af62b54e1d4bbb698",
        "fixtures/native_aot_facade/gói/demo/toán.vi": "3b8b7cc8795a287c154d490901522977c41386372e32f5e9b93b0386f37a75c9",
    },
    "kiem_tra_ham_an_danh_tu_chu": {
        "fixtures/anonymous_functions/entry.vi": "f974f9a59d7ce32cf77dacd5ea5de93ea527dfcdfa5bfc23541ce4cb71e962aa",
        "fixtures/anonymous_modules/entry.vi": "9cdce8babd05c97596abd533d94ed5d901c8875250ae9771adaa2fa231669ef1",
        "fixtures/anonymous_modules/counter.vi": "44946f6a4d4396b3c87fb5df4b5ed61738f1c40e63197f1c83055b1da94a7a91",
        "expected/kiem_tra_ham_an_danh.expected": "96ef6b55c11496e55c3377905c5aca49b4624ee78cbdb7112df01c8fe4034577",
        "expected/kiem_tra_ham_an_danh_module.expected": "7d5bc40f34f5093fa12366e67e47662d2f25d6ce5907db2f82a8145369287cb6",
    },
}

# Direct AST execution is enabled only for golden-verified, hash-pinned sources.
# Cases benchmarked slower under AST retain their dynamic backend.
# Other JIT fixtures keep their existing dynamic backend and are never guessed.
INTERPRETED_CASES = {
    "native_exit_scalar", "native_return_semantics", "native_protected_loop",
    "kiem_tra_native_facade", "native_i32_backend_calls", "native_i32_direct_calls",
    "native_i32_expression_calls",
    "native_aot_index", "native_aot_private_method", "native_frame_pool_gc",
    "native_i32_call_chain", "native_i32_expression", "native_i32_nested_calls",
    "native-source-paths", "native-leaf-architectures",
    "kiem_tra_loi_tep_co_cau_truc",
    "kiem_tra_escape_bien_dich_tu_chu",
    "kiem_tra_lien_ket_tu_chu", "kiem_tra_so_ham_module_tu_chu",
    "kiem_tra_khai_bao_kieu_ffi_tu_chu", "kiem_tra_do_thi_module_tu_chu",
    "kiem_tra_goi_quan_ly_goi", "kiem_tra_native_oop_snapshot",
    "kiem_tra_dns_cau_hinh",
    "kiem_tra_ham_an_danh_tu_chu",
    "kiem_tra_ffi_gio_dia_phuong", "kiem_tra_lien_ket_module_tu_chu",
    "kiem_tra_native_oop",
    "kiem_tra_aot_replace_single_pass", "kiem_tra_dns_cache",
    "kiem_tra_o_cam_tcp_oop",
    "kiem_tra_dinh_dang", "kiem_tra_dns_goi_tin", "kiem_tra_dns_search",
    "kiem_tra_http_response_vpp", "kiem_tra_luong_du_lieu",
    "kiem_tra_quoc_te_hoa", "kiem_tra_vm_ieee754",
    "kiem_tra_package_tieng_viet", "kiem_tra_ast_oop",
    "kiem_tra_chuoi_nen_tang",
    "kiem_tra_aot_decimal_scan", "kiem_tra_aot_type_branch",
    "kiem_tra_aot_byte_scan_set", "kiem_tra_aot_i32_run", "kiem_tra_aot_index_offset",
    "kiem_tra_aot_map_remove_value", "kiem_tra_aot_utf8_word_scan",
    "kiem_tra_opcode_chon_ca", "kiem_tra_rc_hardening",
    "kiem_tra_bo_dang_ky_ham", "kiem_tra_du_lieu_oop",
    "kiem_tra_luong_tu_qiskit", "kiem_tra_rest_json_jwt",
    "kiem_tra_tuong_thich_oop_toan_thu_vien",
    "anonymous_functions", "import_main", "kiem_tra_bo_qua", "kiem_tra_boolean", "kiem_tra_chon_ca",
    "kiem_tra_bo_noi_chuoi", "kiem_tra_stdlib", "kiem_tra_stdlib_tinh_toan",
    "kiem_tra_stdlib_mo_rong",
    "kiem_tra_chuoi_co_ban", "kiem_tra_gia_tri", "kiem_tra_goi_dung",
    "kiem_tra_goi_ham", "kiem_tra_goi_van_ban", "kiem_tra_goi_tuy_chon", "kiem_tra_rong_va_map",
    "kiem_tra_goi_cau_hinh", "kiem_tra_ha_tang_oop", "kiem_tra_goi_nhat_ky_1_0",
    "kiem_tra_json_an_toan", "kiem_tra_goi_thoi_gian",
    "kiem_tra_tu_vung_oop", "kiem_tra_van_ban_oop", "kiem_tra_tien_ich_oop",
    "kiem_tra_stdlib_crypto", "kiem_tra_goi_json", "kiem_tra_goi_mang",
    "kiem_tra_bien_dich_bieu_thuc_vi", "kiem_tra_json_escape_byte_view",
    "kiem_tra_json_dieu_khien", "kiem_tra_json_integer_overflow_scan", "kiem_tra_goi_mat_ma",
    "kiem_tra_json_key_scan", "kiem_tra_json_utf8", "kiem_tra_duong_dan_windows",
    "kiem_tra_goi_ban_dia_hoa", "kiem_tra_thoi_gian_lich",
    "kiem_tra_constructor_tham_so", "kiem_tra_cu_phap_modifier_cu", "kiem_tra_de_quy",
    "kiem_tra_giao_dien_trien_khai", "kiem_tra_hoi_quy_tong_hop",
    "kiem_tra_khai_bao_bien_runtime", "kiem_tra_kieu_dong_call_boundary",
    "kiem_tra_dieu_kien_long_nhieu_cap", "kiem_tra_dieu_kien_phu_dinh",
    "kiem_tra_ham", "kiem_tra_ham_4_tham_so", "kiem_tra_ham_an_danh",
    "kiem_tra_ham_an_danh_module",
    "kiem_tra_ham_da_tu_khong_nhay", "kiem_tra_ham_tham_so", "kiem_tra_ke_thua",
    "kiem_tra_khi", "kiem_tra_lambda_hof_mac_dinh", "kiem_tra_lop_phuong_thuc_forward",
    "kiem_tra_lop_truy_cap", "kiem_tra_mang_3_chieu",
    "kiem_tra_ngoai_le", "kiem_tra_ngoai_le_xuyen_ham",
    "kiem_tra_nhanh_dieu_kien_moi", "kiem_tra_noi_chuoi", "kiem_tra_object_model",
    "kiem_tra_receiver_minh", "kiem_tra_receiver_minh_de_quy",
    "kiem_tra_receiver_minh_dieu_khien", "kiem_tra_receiver_minh_nhieu_instance",
    "kiem_tra_so_chan_1-20", "kiem_tra_so_chia_het_cho_3_va_4",
    "kiem_tra_so_le_chia_het_cho_5",
    "kiem_tra_semantics_gia_tri", "kiem_tra_so_nguyen", "kiem_tra_so_thuc",
    "kiem_tra_thoat_vong_lap",
    "kiem_tra_ten_kieu_nhieu_tu", "kiem_tra_toan_tu_bit",
    "kiem_tra_toan_tu_moi", "kiem_tra_tra_ve", "kiem_tra_uu_tien_diem_vao",
    "kiemtramacdinh",
    "kiem_tra_closure_capture",
    "kiem_tra_collection_bai_63_75", "kiem_tra_collections_oop",
    "kiem_tra_doc_chi_so_ket_qua_goi", "kiem_tra_goi_bo_suu_tap",
    "kiem_tra_goi_ngau_nhien", "kiem_tra_goi_nhat_ky_moi",
    "kiem_tra_goi_bang_ma", "kiem_tra_goi_db_config", "kiem_tra_goi_xml",
    "kiem_tra_ffi_thu_muc", "kiem_tra_ffi_utc", "kiem_tra_ffi_moi_truong",
    "kiem_tra_ffi_don_dieu", "kiem_tra_goi_thoi_gian_1_0",
    "kiem_tra_package_modules",
    "kiem_tra_goi_xml_dom", "kiem_tra_ffi_entropy",
    "kiem_tra_file_dem", "native_path_kind",
    "kiem_tra_goi_hoa_hoc", "kiem_tra_goi_kiem_thu", "kiem_tra_goi_regex",
    "kiem_tra_goi_tap_ban_ghi", "kiem_tra_goi_toan_hoc", "kiem_tra_goi_vat_ly",
    "kiem_tra_database_oop", "kiem_tra_khung_bien_module", "kiem_tra_luong_tu_openqasm",
    "kiem_tra_module_alias_lifecycle", "kiem_tra_module_alias_trung",
    "kiem_tra_module_khoi_tao_mot_lan", "kiem_tra_module_lifecycle",
    "kiem_tra_module_reexport", "kiem_tra_ngan_xep_oop", "kiem_tra_namespace_module",
    "kiem_tra_package_09", "kiem_tra_ung_dung_oop", "kiểm tra tên tiếng Việt cách từ",
    "kiem_tra_visibility_instance", "kiemtrathamsotrung", "program",
    "kiem_tra_aot_tang",
    "kiem_tra_aot_string_intrinsics",
    "kiem_tra_application_server",
    "kiem_tra_embedded_vm",
    "kiem_tra_diem_vao_ke_thua",
    "kiem_tra_diem_vao_lop_chinh",
    "kiem_tra_diem_vao_mac_dinh",
    "kiem_tra_bien_dich_khi",
    "kiem_tra_ffi_local_windows",
    "kiem_tra_ffi_socket_loi",
    "kiem_tra_dich_vu",
    "kiem_tra_ffi_tls",
    "kiem_tra_ffi_thu_muc_windows",
    "kiem_tra_bien_dich_khi_vm",
    "kiem_tra_ffi_http_transport_bat_loi",
    "kiem_tra_file_stream_oop",
    "kiem_tra_gc_mvp",
    "kiem_tra_goi_bien_dich",
    "kiem_tra_goi_kich_ban",
    "kiem_tra_goi_cong_cu_luu_tru",
    "kiem_tra_goi_may_chu_http",
    "kiem_tra_goi_luu_tru",
    "kiem_tra_goi_repl",
    "kiem_tra_goi_soan_thao_tuong_tac",
    "kiem_tra_http_may_chu_thuan_vpp",
    "kiem_tra_http_client_oop",
    "kiem_tra_jit_mvp",
    "kiem_tra_http_thuan_vpp",
    "kiem_tra_goi_zipfs",
    "kiem_tra_http_transport_bat_loi",
    "kiem_tra_json_byte_view",
    "kiem_tra_json_file_roundtrip",
    "kiem_tra_json_mixed_i32_run",
    "kiem_tra_kien_truc_oop_compiler",
    "kiem_tra_logger_oop",
    "kiem_tra_nhat_ky", "kiem_tra_lexer_token_gon", "kiem_tra_stdlib_starter",
    "kiem_tra_stdlib_nen_tang", "kiem_tra_tong_hop_khong_xung_dot",
    "kiem_tra_su_kien", "kiem_tra_quet_tach_chuoi",
    "kiem_tra_api_db_project",
    "kiem_tra_tuy_chon_thong_ke", "kiem_tra_stdlib_io_config_time",
    "kiem_tra_bien_dich_chuong_trinh_vi",
    "kiem_tra_bien_dich_tu_vung_vi", "kiem_tra_chi_muc_ast_bo_kiem_thu",
    "kiem_tra_oop_bytecode_tu_chu", "kiem_tra_quy_trinh_oop",
    "kiem_tra_tap_hop_bien_dich_tu_chu",
    "kiem_tra_aot_type_branch_missing", 
    "kiem_tra_gan_ket_hop", 
    "kiem_tra_sort_early_exit",
    "kiem_tra_diem_vao_ke_thua_module",
    "kiem_tra_api_thuc_thu", "kiem_tra_phien_bien_dich_oop",
    "kiem_tra_ffi_tien_trinh", "kiem_tra_dns_transport_day_du",
    "kiem_tra_socket_ip", "kiem_tra_process_quote_windows",
}


def fixture_free(case, source):
    diagnostic_case = case["id"] in INTERPRETED_RUNTIME_DIAGNOSTICS
    allowed_keys = JIT_CASE_KEYS | ({"historical_expected", "run_diagnostic_regex"} if diagnostic_case else set())
    if case.get("suite") != "full" or set(case) - allowed_keys:
        return False
    if diagnostic_case and (case.get("exit") != 1 or not case.get("run_diagnostic_regex")):
        return False
    digest = FIXTURE_FREE_SOURCES.get(case["source"])
    return digest is not None and hashlib.sha256(
        source.read_bytes().replace(b"\r\n", b"\n")).hexdigest() == digest


def reviewed_imported(case, source):
    dependencies = REVIEWED_SOURCE_DEPENDENCIES.get(case["id"], {})
    reviewed_timezone = (case["id"] == "kiem_tra_ffi_gio_dia_phuong" and
                         case.get("timezone") == "EST5EDT,M3.2.0/2,M11.1.0/2")
    allowed_keys = (JIT_CASE_KEYS | ({"sources"} if dependencies else set()) |
                    ({"timezone"} if reviewed_timezone else set()))
    if (case.get("suite") != "full" or set(case) - allowed_keys or
            case.get("sources", []) != list(dependencies)):
        return False
    digest = REVIEWED_IMPORTED_SOURCES.get(case["source"])
    if digest is None or hashlib.sha256(
            source.read_bytes().replace(b"\r\n", b"\n")).hexdigest() != digest:
        return False
    return all((ROOT / path).is_file() and hashlib.sha256(
        (ROOT / path).read_bytes().replace(b"\r\n", b"\n")).hexdigest() == expected
        for path, expected in dependencies.items()) and all(
        (TESTS / path).is_file() and hashlib.sha256(
            (TESTS / path).read_bytes().replace(b"\r\n", b"\n")).hexdigest() == expected
        for path, expected in REVIEWED_RELATIVE_FIXTURES.get(case["id"], {}).items())


def interpretable(case):
    return ((case["id"] in INTERPRETED_CASES or case["id"] in INTERPRETED_RUNTIME_DIAGNOSTICS) and
            (fixture_free(case, ROOT / case["source"]) or
             reviewed_imported(case, ROOT / case["source"])))


def toolchain_reason(case, *, jit_mode=False):
    """Cases whose contract requires a specific compiler or executable."""
    if jit_mode and ("dump" in case or case.get("cmake") in JIT_I32_HARNESSES or
                     case.get("cmake") == "check_native_runtime.cmake" or
                     case.get("linker_phase") in {"reader", "layout", "patch", "executable", "direct-object", "elf-partial"} or
                     case.get("vm_phase") in JIT_VM_PHASES):
        return None
    if any(key in case for key in HARNESS_KEYS):
        return "compiler/linker/VM artifact assertion"
    if case.get("tooling_test"):
        if jit_mode and case["id"] in {"kiem_tra_chia_nhom_golden", "kiem_tra_do_thi_build", "kiem_tra_regression_runner"}:
            return None
        return "build tooling and executable fixture"
    if case.get("dns_mock") and not jit_mode:
        return "DNS fixture requires the native i32 socket ABI"
    return None


def jit_eligible(case, experimental=False):
    """Explicit JIT mode runs every source case outside the native toolchain."""
    return not toolchain_reason(case, jit_mode=experimental) and (
        experimental or case.get("jit", False) or case.get("compile_reject", False))


def jit_environment(env):
    env = dict(env, VPP_AOT_ASSEMBLER="/assembler-must-not-run",
               VPP_AOT_LINKER="/linker-must-not-run")
    env["PATH"] = str(Path(os.environ["SystemRoot"]) / "System32") if os.name == "nt" else ""
    env.pop("VPP_JIT_PROFILE", None)
    env.pop("VPP_JIT_TIER", None)
    return env


def module_source(case):
    """Adapt a reviewed function-only fixture without merging its imports."""
    if case.get("suite") != "full" or set(case) - {
            "id", "suite", "source", "expected", "http", "smoke", "jit"} or "expected" not in case:
        return None
    source = (ROOT / case["source"]).read_text(encoding="utf-8")
    # Only imports/comments before the entry function; no argument or relative
    # import dependency. The allowlist is intentional, not automatic discovery.
    prefix = re.match(r"(?m)\A(?:\s|//[^\n]*\n|nhập [^;\n]+;)*", source)
    entry = re.match(r"hàm chính\(\)\s*\{", source[prefix.end():])
    # Ignore comments and quoted literals when checking the function body.
    # Keep the original source intact for compilation and diagnostics.
    code = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  ' ', source, flags=re.S)
    if (not entry or code.count("hàm chính()") != 1
            or re.search(r"nhập\s+\.|đối số|đường dẫn thực thi|trả về", code)):
        return None
    body = code[code.index("hàm chính()") + len("hàm chính()"):].lstrip()
    depth = 0
    for i, char in enumerate(body):
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                if not re.fullmatch(r"\s*;?\s*", body[i + 1:]):
                    return None
                break
    else:
        return None
    start = prefix.end()
    return source[:start] + source[start:].replace(
        "hàm chính()", "hàm công khai kiểm thử()", 1)


def plan_module_suites(cases):
    by_id = {case["id"]: case for case in cases}
    if len(by_id) != len(cases):
        raise ValueError("duplicate regression IDs in module selection")
    suites = {}
    seen = set()
    for name, members in MODULE_SUITES.items():
        if not 2 <= len(members) <= 9 or len(set(members)) != len(members) or seen.intersection(members):
            raise ValueError(f"invalid module suite membership: {name}")
        seen.update(members)
        selected = []
        for suffix in members:
            case = by_id.get("kiem_tra_" + suffix)
            if case is not None:
                source = module_source(case)
                if source is not None:
                    selected.append((case, source))
        if len(selected) >= 2:
            suites[name] = selected
    return suites


def build_module_suite(name, members, compiler, output, logs, env, timeout):
    """Compile one executable, dispatched in a new process for every case."""
    logs.mkdir(parents=True, exist_ok=True)
    imports, branches = [], []
    for i, (case, source) in enumerate(members):
        (logs / f"case{i}.vi").write_text(source, encoding="utf-8")
        imports.append(f"nhập case{i} như ca{i};")
        branches.append(f"    nếu (k == {48 + i}) {{ ca{i}.kiểm thử(); trả về 0; }};")
    entry = logs / "suite.vi"
    entry.write_text("\n".join(imports + [
        "hàm chính() {",
        "    nếu (đối số đếm() != 2) { trả về 97; };",
        "    h = đối số lấy(1);",
        "    nếu (byte dài(h) != 1) { byte hủy(h); trả về 97; };",
        "    k = byte lấy(h, 0); byte hủy(h);",
        *branches, "    trả về 97;", "};", "",
    ]), encoding="utf-8")
    (logs / "members.json").write_text(json.dumps(
        [case["id"] for case, _ in members], ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"BUILD MODULE {name}: {len(members)} cases, one executable", flush=True)
    module_env = dict(env, VPP_PREFERENCES_HOME=str(logs / "preferences"))
    if module_env.get("VPP_COMPILER_PROFILE_FILE"):
        module_env["VPP_COMPILER_PROFILE_FILE"] = str(logs / "compiler.profile")
    status = build(compiler, entry, output, ROOT, module_env, logs, timeout * len(members))
    if status or not output.is_file():
        raise RuntimeError(f"module {name}: build exit {status} or missing executable; logs: {logs}")


def normalize(data):
    return data.replace(b"\r\n", b"\n")


def normalize_stderr(data):
    # The runner executes in an isolated temporary directory, so source
    # diagnostics legitimately contain absolute repository paths. Normalize
    # only the two source path fields of a stack frame, retaining the full
    # diagnostic, function name, line, column and module identity.
    prefixes = {str(ROOT).encode("utf-8") + os.sep.encode(),
                ROOT.as_posix().encode("utf-8") + b"/"}

    def relative(path):
        for prefix in prefixes:
            if path.startswith(prefix):
                return path[len(prefix):].replace(b"\\", b"/")
        return path

    def frame(match):
        return (match[1] + relative(match[2]) + match[3] +
                relative(match[4]) + match[5])

    pattern = r"(?m)^(  ở [^\n]+? \()([^\n]+?)(:\d+:\d+\) \[mô đun=)([^\n]+)(\])$"
    return re.sub(pattern.encode("utf-8"), frame, normalize(data))


def executable_name(name):
    return name + ".exe" if platform.system() == "Windows" else name


def partition(cases, count, module_suites=False):
    # Historical timings are scheduling hints only; the new backend can differ.
    weights = {}
    for line in (ROOT / "scripts/quality/native_golden_costs.tsv").read_text(encoding="utf-8").splitlines():
        if line.strip() and not line.startswith("#"):
            name, weight = line.split("\t")
            weights[name] = int(weight)
    def cost(case):
        if "pending" in case:
            return 0
        return case.get("weight_ms", weights.get(case["id"], 2000))
    shards = [[] for _ in range(count)]
    totals = [0] * count
    for case in cases:
        if case.get("http"):
            shards[0].append(case)
            totals[0] += cost(case)
    # Keep reviewed module executables intact across CI shards. Otherwise
    # sharding turns a single shared build back into several independent ones.
    units = []
    grouped = set()
    if module_suites and count > 1:
        for members in plan_module_suites(cases).values():
            group = [case for case, _ in members]
            grouped.update(c["id"] for c in group)
            if any(c.get("http") for c in group):
                for c in group:
                    if not c.get("http"):
                        shards[0].append(c)
                        totals[0] += cost(c)
            else:
                units.append(group)
    units.extend([c] for c in cases if not c.get("http") and c["id"] not in grouped)
    for unit in sorted(units, key=lambda u: (-sum(map(cost, u)), u[0]["id"])):
        index = min(range(count), key=lambda i: (totals[i], len(shards[i]), i))
        shards[index].extend(unit)
        totals[index] += sum(map(cost, unit))
    return shards


def load_manifest():
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    cases = manifest["cases"]
    ids = [case["id"] for case in cases]
    if len(ids) != len(set(ids)):
        raise ValueError("duplicate regression IDs")
    for case in cases:
        if case.get("suite") not in ("full", "i32", "host", "guest"):
            raise ValueError(f"unknown regression suite: {case['id']}: {case.get('suite')}")
        if case["suite"] in ("host", "guest") and case.get("smoke"):
            raise ValueError(f"backend-specific regression cannot be a mixed smoke case: {case['id']}")
        if "expected_stderr" in case:
            if not isinstance(case["expected_stderr"], str) or not case["expected_stderr"].endswith(".expected"):
                raise ValueError(f"invalid stderr golden path: {case['id']}")
            if "diagnostic" in case or "run_diagnostic_regex" in case:
                raise ValueError(f"ambiguous stderr golden contract: {case['id']}")
            if ("expected" in case) == ("stdout" in case):
                raise ValueError(f"stderr golden requires exactly one stdout contract: {case['id']}")
    for kind, registered in (
        (".vi", {case["source"] for case in cases} | {source for case in cases for source in case.get("sources", [])} | set(manifest["unregistered_sources"])),
        (".expected", {case[key] for case in cases for key in ("expected", "expected_stderr", "historical_expected") if key in case}),
    ):
        actual = {p.relative_to(ROOT).as_posix() for p in TESTS.rglob("*" + kind)}
        actual = {p for p in actual if not any(part.startswith(".") for part in Path(p).parts)}
        if actual != registered:
            raise ValueError(f"{kind} inventory mismatch: new={sorted(actual-registered)}, missing={sorted(registered-actual)}")
    for case in cases:
        if case.get("jit") and (case["suite"] != "full" or set(case) - JIT_CASE_KEYS):
            raise ValueError(f"unsupported JIT case configuration: {case['id']}")
    full = [c for c in cases if c["suite"] == "full"]
    shards = partition(full, 1)
    assigned = [c["id"] for shard in shards for c in shard]
    if (sorted(assigned) != sorted(c["id"] for c in full)
            or any(not any("pending" not in c for c in shard) for shard in shards)
            or any(c.get("http") for shard in shards[1:] for c in shard)):
        raise ValueError("invalid full regression shard assignment")
    return manifest


def fixture_process(command, env):
    """Launch the isolated runner self-test's process doubles.

    Enabled only by the nested runner self-test's explicit CLI flag. Copies
    must live within its isolated root; ordinary commands keep their real ABI.
    """
    command = [str(x) for x in command]
    if FIXTURE_EXECUTABLE is None:
        return command, env
    executable = Path(command[0]).resolve()
    if not executable.is_relative_to(ROOT) or not executable.is_file():
        return command, env
    # Only published mock executables are redirected. Python, CMake and other
    # host tools (including test scripts under ROOT) are never intercepted.
    if hashlib.sha256(executable.read_bytes()).digest() != hashlib.sha256(FIXTURE_EXECUTABLE.read_bytes()).digest():
        return command, env
    source = ROOT / "scripts/quality/regression_runner_mock.py"
    if env.get("VPP_TEST_FAKE") != "1" or not source.is_file():
        raise RuntimeError("invalid runner JIT fixture context")
    return [sys.executable, str(source), str(executable), *command[1:]], env


def execute(command, cwd, env, logs, name, timeout, *, stdin=None):
    start = time.monotonic()
    command, env = fixture_process(command, env)
    with (logs / (name + ".stdout")).open("wb") as out, (logs / (name + ".stderr")).open("wb") as err, (stdin.open("rb") if stdin is not None else contextlib.nullcontext(subprocess.DEVNULL)) as input_stream:
        process = subprocess.Popen([str(x) for x in command], cwd=cwd, env=env,
                                   stdin=input_stream, stdout=out, stderr=err,
                                   start_new_session=(os.name != "nt"))
        try:
            status = process.wait(timeout=timeout)
        except BaseException:
            if process.poll() is None:
                if os.name == "nt":
                    process.kill()
                else:
                    os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            raise
        finally:
            (logs / (name + ".seconds")).write_text(f"{time.monotonic() - start:.2f}\n")
    (logs / (name + ".status")).write_text(str(status))
    return status


def build(compiler, source, binary, cwd, env, logs, timeout):
    return execute([compiler, "build", source, "-o", binary], cwd, env, logs, "build", timeout)


def batch_eligible(case):
    # Only ordinary positive builds with identical compiler environments.
    # Keep compiler rejection, custom environments and harnesses independent.
    return case["suite"] == "full" and not any(key in case for key in (
        "dump", "linker_phase", "vm_phase", "cmake", "compile_reject",
        "diagnostic", "env", "timezone", "dns_mock", "tooling_test"))


def build_batch(cases, compiler, directories, logs, env, timeout, group):
    """Return verified publications and a failure reason, if any."""
    batch_logs = logs / "batches" / str(group)
    batch_logs.mkdir(parents=True)
    batch_env = dict(env, VPP_NATIVE_NO_CACHE="0",
                     VPP_PREFERENCES_HOME=str(batch_logs / "preferences"))
    if batch_env.get("VPP_COMPILER_PROFILE_FILE"):
        batch_env["VPP_COMPILER_PROFILE_FILE"] = str(batch_logs / "compiler.profile")
    command = [compiler, "batch-build"]
    for case in cases:
        command.extend((ROOT / case["source"], directories[case["id"]] / executable_name("program")))
    failure = ""
    try:
        status = execute(command, ROOT, batch_env, batch_logs, "batch", timeout * len(cases))
        if status:
            failure = f"batch exit {status}"
    except (OSError, subprocess.TimeoutExpired) as exc:
        failure = str(exc)
        (batch_logs / "batch.error").write_text(failure, encoding="utf-8")
    completed = {}
    stdout = batch_logs / "batch.stdout"
    if stdout.is_file():
        for line in stdout.read_text(encoding="utf-8", errors="replace").splitlines():
            if not line.startswith("VPP_BATCH_BUILD "):
                continue
            match = re.fullmatch(r"VPP_BATCH_BUILD (\d+) (\S+)", line)
            try:
                index, milliseconds = int(match[1]), float(match[2])
            except (TypeError, ValueError):
                failure = failure or "invalid batch completion marker"
                break
            if (index != 2 * len(completed) + 1 or index >= 2 * len(cases)
                    or not math.isfinite(milliseconds) or milliseconds < 0):
                failure = failure or "invalid batch completion order or duration"
                break
            name = cases[(index - 1) // 2]["id"]
            if not (directories[name] / executable_name("program")).is_file():
                failure = failure or "batch did not publish executable"
                break
            completed[name] = milliseconds / 1000
    if len(completed) != len(cases):
        failure = failure or "missing batch completion markers"
    # A nonzero exit after all publications still fails the final case.
    if failure and len(completed) == len(cases):
        completed.pop(cases[-1]["id"])
    for case in cases:
        name = case["id"]
        directory = logs / name
        directory.mkdir(exist_ok=True)
        (directory / "build.batch").write_text(f"batches/{group}\n")
        if name in completed:
            (directory / "build.seconds").write_text(f"{completed[name]:.2f}\n")
            (directory / "build.status").write_text("0")
        else:
            # Do not run or retry an unverified publication: that could mask
            # a compiler session failure.
            (directories[name] / executable_name("program")).unlink(missing_ok=True)
            (directory / "build.status").write_text("1")
            for suffix in ("stdout", "stderr", "error"):
                source = batch_logs / f"batch.{suffix}"
                if source.is_file():
                    shutil.copyfile(source, directory / f"build.{suffix}")
    return completed, failure


def prepare_vm(compiler, logs, env, timeout):
    # CI packages one VM per platform. Local runs build it once.
    packaged = compiler.with_name(executable_name("vpp-vm"))
    if packaged.is_file():
        print(f"SETUP shared VM: {packaged}", flush=True)
        return packaged
    directory = logs / "shared-vm"
    directory.mkdir(parents=True, exist_ok=True)
    vm = directory / executable_name("vpp-vm")
    vm_env = dict(env)
    if platform.system() == "Linux":
        vm_env["VPP_NATIVE_ELF_LINK"] = "1"
    print("SETUP building shared VM", flush=True)
    i32 = compiler.with_name(executable_name("vpp-i32"))
    source = ROOT / "gói/máy ảo/chính.vi"
    if platform.system() == "Windows":
        from check_windows_full_bootstrap import raw_build
        raw_build(i32, source, vm, vm_env, directory, vm_core=True, timeout=timeout)
        status = 0
    elif platform.system() == "Darwin" and platform.machine() == "arm64":
        status = execute(["/bin/bash", ROOT / "scripts/macos/build-vpp-vm.sh", i32, vm],
                         ROOT, vm_env, directory, "build", timeout)
    else:
        status = build(i32, source, vm, ROOT, vm_env, directory, timeout)
    if status or not vm.is_file():
        raise RuntimeError(f"Shared VM build failed (exit {status}); logs: {directory}")
    return vm


def verify_failure(case, status, logs, stage):
    if status != case["exit"]:
        raise RuntimeError(f"{stage} exit {status}, expected {case['exit']}")
    combined = (logs / (stage + ".stdout")).read_bytes() + (logs / (stage + ".stderr")).read_bytes()
    if case["diagnostic"].encode("utf-8") not in combined:
        raise RuntimeError(f"{stage} missing diagnostic: {case['diagnostic']}")


def jit_dump(case, jit, source, cwd, env, logs, timeout):
    # Execute the same compiler library functions used by the native CLI.
    common = "nhập gói.nhập xuất.tệp.đọc tệp;\nnhập gói.hệ thống.môi trường.đọc biến môi trường;\n"
    if case["dump"] == "disassemble":
        imports = ("nhập gói.biên dịch.nguồn tệp.biên dịch nguồn tệp tự chủ;\n"
                   "nhập gói.biên dịch.bộ nạp.biên dịch nạp sản phẩm biên dịch;\n"
                   "nhập gói.biên dịch.giải mã bytecode.biên dịch giải mã chương trình;\n")
        expression = 'biên dịch giải mã chương trình(biên dịch nạp sản phẩm biên dịch(biên dịch nguồn tệp tự chủ(đường, đọc biến môi trường("VPP_HOME", ""))))'
    else:
        function = {"dump-ast": "biên dịch hiển thị cây cú pháp", "dump-ir": "biên dịch hiển thị biểu diễn trung gian"}[case["dump"]]
        imports = "nhập gói.biên dịch.hiển thị cây và IR." + function + ";\n"
        expression = function + "(đọc tệp(đường))"
    harness = cwd / "dump.vi"
    harness.write_text(common + imports + 'hàm chính() { đường = đọc biến môi trường("VPP_TEST_SOURCE", ""); in ' + expression + '; };\n', encoding="utf-8")
    env = dict(jit_environment(env), VPP_TEST_SOURCE=str(source))
    for name in ("first", "second"):
        if execute([jit, "--dynamic", harness], cwd, env, logs, name, timeout):
            raise RuntimeError(f"JIT {case['dump']} failed")
        if (logs / (name + ".stderr")).read_bytes():
            raise RuntimeError(f"JIT {case['dump']} emitted stderr")
    first = (logs / "first.stdout").read_bytes()
    if not first.strip() or first != (logs / "second.stdout").read_bytes():
        raise RuntimeError(f"{case['dump']} produced empty or unstable output")


def jit_i32_print(jit, source, cwd, env, logs, timeout):
    env = jit_environment(env)
    def check(path, label, status, stdout=None, diagnostic=None):
        actual = execute([jit, path], cwd, env, logs, label, timeout)
        out = (logs / (label + ".stdout")).read_bytes()
        err = (logs / (label + ".stderr")).read_bytes()
        if actual != status or (stdout is not None and normalize(out) != normalize(stdout)) or (diagnostic and diagnostic.encode() not in out + err):
            raise RuntimeError(f"JIT i32 print {label}: unexpected exit/output ({actual})")
        if diagnostic is None and err:
            raise RuntimeError(f"JIT i32 print {label}: unexpected stderr")
    check(source, "run", 7, b"-2147483648\n42\n0\n")
    for label, code, status, stdout, diagnostic in (
        ("print-variable", "hàm chính() { biến số = 42; in số; trả về 0; };", 0, b"42\n", None),
        ("string-print", "hàm chính() { in ; trả về 0; };", 1, None, "thiếu toán hạng"),
        ("legacy-print", "hàm chính() { in số(7); trả về 0; };", 1, None, "hàm chưa định nghĩa"),
        ("reserved-symbol", "hàm vpp_native_print_i32(x) { trả về x; }; hàm main() { trả về 0; };", 1, None, "dành riêng"),
    ):
        probe = cwd / (label + ".vi")
        probe.write_text(code + "\n", encoding="utf-8")
        check(probe, label, status, stdout, diagnostic)


def jit_i32_io(case, jit, source, cwd, env, logs, timeout):
    """Preserve the independent byte/file/stdin oracles of the native harnesses."""
    env = jit_environment(env)
    kind = case["cmake"]

    def check(path, label, stdout=None, diagnostic=None, stdin=None):
        # Standalone i32 literals and byte/file handles need a fresh arena,
        # rather than the compiler's ordinary function-call arena.
        status = execute([jit, "--raw-program", path], cwd, env, logs, label, timeout, stdin=stdin)
        out = (logs / (label + ".stdout")).read_bytes()
        err = (logs / (label + ".stderr")).read_bytes()
        if diagnostic is not None:
            if status != 1 or diagnostic.encode() not in out + err:
                raise RuntimeError(f"{label}: expected JIT rejection {diagnostic!r}, got {status}")
        elif status or normalize(out) != normalize(stdout) or err:
            raise RuntimeError(f"{label}: unexpected JIT output/exit {status}")

    if kind == "checknativebulk.cmake":
        check(source, "run", "4096\n4096\n1820\n0\nNative UTF-8: Việt Nam\n".encode())
        payload = (cwd / "native-block.bin").read_bytes()
        if len(payload) != 10012 or hashlib.sha256(payload).hexdigest() != "4a3fb371a00b860295a5096738c28d7dbedd444d52277cc992187b950a0e1047":
            raise RuntimeError("native bulk file size/hash mismatch")
        arity = "tệp đọc khối(1)"
        reserved = ("byte nối", "byte bằng", "byte in", "tệp đọc khối", "tệp ghi khối",
                    "vpp_native_bytes_concat", "vpp_native_file_read_block")
    elif kind == "checknativebytefile.cmake":
        check(source, "run", b"4\n330\n-1\n-2\n")
        for name in ("vpp-io.in", "vpp-io.out"):
            if (cwd / name).read_bytes() != bytes.fromhex("00ff0a41"):
                raise RuntimeError(f"{name}: incorrect binary bytes")
        arity = "byte thêm(1)"
        reserved = ("byte mới", "tệp đóng", "vpp_native_bytes_get", "vpp_native_file_close")
    elif kind == "checknativebytesops.cmake":
        check(source, "run", b"2097152\n90\n")
        if (cwd / "native-slice-cap.bin").read_bytes() != b"A":
            raise RuntimeError("native slice boundary file mismatch")
        arity = "byte cắt(1, 0)"
        reserved = ("byte cắt", "byte tìm", "byte làm rỗng", "vpp_native_bytes_slice",
                    "vpp_native_bytes_find", "vpp_native_bytes_clear")
    elif kind == "checknativeread.cmake":
        for label, data, expected in (
            ("boundary", b"-2147483648\n2147483647\n", b"-2147483648\n0\n2147483647\n0\n"),
            ("invalid", b"12abc\n-1\n", b"0\n2\n-1\n0\n"),
            ("overflow", b"2147483648\n0\n", b"0\n3\n0\n0\n"),
            ("eof", b"", b"0\n1\n0\n1\n"),
            ("longline", b"9" * 80 + b"\n17\n", b"0\n2\n17\n0\n"),
            ("whitespace", b"  +42  \n-0\n", b"42\n0\n0\n0\n"),
        ):
            input_file = cwd / (label + ".txt")
            input_file.write_bytes(data)
            check(source, label, expected, stdin=input_file)
        arity = "đọc số(12)"
        reserved = ("đọc số", "vpp_native_read_i32", "vpp_native_read_status")
    else:
        raise ValueError(kind)
    probe = cwd / "bad-arity.vi"
    probe.write_text(f"hàm chính() {{ trả về {arity}; }};\n", encoding="utf-8")
    check(probe, "bad-arity", diagnostic="không nhận đối số" if kind == "checknativeread.cmake" else "sai số đối số")
    for i, name in enumerate(reserved):
        probe = cwd / f"reserved-{i}.vi"
        probe.write_text(f"hàm {name}() {{ trả về 0; }}; hàm chính() {{ trả về 0; }};\n", encoding="utf-8")
        check(probe, f"reserved-{i}", diagnostic="dành riêng")


def jit_i32_frontend(case, jit, source, cwd, env, logs, timeout):
    """Run the original byte-handle lexer/parser harness without an executable."""
    env = jit_environment(env)
    native = ROOT / "gói/biên dịch/bản địa"
    parts = [native / "phân tích từ vựng.vi"]
    parser = case["cmake"] == "checknativeparser.cmake"
    if parser:
        parts.extend(native / name for name in ("từ tố.vi", "phân tích cú pháp.vi", "biểu diễn trung gian.vi"))
        (cwd / "native-parser-input.vi").write_bytes("// ví dụ\r\nhàm chính() { biến a = 12; trả về a * 4 + 4; };\n".encode())
    else:
        (cwd / "native-lexer-input.vi").write_bytes('// header\r\nhàm mẫu(a) { /* chú */ biến x = 12; in "Chào\\n"; trả về x; };\r\n'.encode())
    parts.append(source)
    combined = cwd / "frontend.vi"
    combined.write_text("\n".join(re.sub(r"(?m)^[ \t]*nhập[^\n]*;[ \t]*$", "", part.read_text(encoding="utf-8")) for part in parts), encoding="utf-8")
    status = execute([jit, "--raw-program", combined], cwd, env, logs, "run", timeout)
    if status or normalize((logs / "run.stdout").read_bytes()) != b"20261017\n" or (logs / "run.stderr").read_bytes():
        raise RuntimeError(f"native JIT frontend failed: {status}")
    if not parser:
        return
    expected = bytes.fromhex("010000000c0000000300000000000000020000000000000001000000040000000600000000000000010000000400000004000000000000000700000000000000")
    if (cwd / "native-parser-output.ir").read_bytes() != expected:
        raise RuntimeError("native parser IR differs from independent wire oracle")
    reserved = ("byte đẩy số nguyên ba mươi hai bit", "byte đọc số nguyên ba mươi hai bit",
                "byte ghi số nguyên ba mươi hai bit", "vpp_native_bytes_push_i32",
                "vpp_native_bytes_load_i32", "vpp_native_bytes_store_i32", "máy chia số nguyên ba mươi hai bit")
    probes = [(f"reserved-{i}", f"hàm {name}() {{ trả về 0; }}; hàm chính() {{ trả về 0; }};", "dành riêng") for i, name in enumerate(reserved)]
    probes.extend((label, f"hàm chính() {{ trả về {call}; }};", "sai số đối số") for label, call in (
        ("bad-parser-arity", "byte ghi số nguyên ba mươi hai bit(1, 2)"),
        ("bad-native-div-arity", "máy chia số nguyên ba mươi hai bit(52)")))
    for label, code, diagnostic in probes:
        path = cwd / (label + ".vi")
        path.write_text(code + "\n", encoding="utf-8")
        status = execute([jit, "--raw-program", path], cwd, env, logs, label, timeout)
        if status != 1 or diagnostic.encode() not in (logs / (label + ".stdout")).read_bytes() + (logs / (label + ".stderr")).read_bytes():
            raise RuntimeError(f"{label}: native JIT parser did not reject with {diagnostic}")


def jit_i32_codegen(jit, source, cwd, env, logs, timeout):
    """Keep the raw codegen oracle and execute the exact emitted Mach-O object."""
    from check_native_macho_layout import parse, catalog, verify_native_signature
    env = jit_environment(env)
    native = ROOT / "gói/biên dịch/bản địa"
    parts = [native / name for name in ("phân tích từ vựng.vi", "từ tố.vi", "phân tích cú pháp.vi",
             "biểu diễn trung gian.vi", "sinh mã.vi", "đối tượng.vi")]
    (cwd / "native-codegen-input.vi").write_text(
        "hàm chính() { biến x = -159; x /= -3; x %= 52; x += 51; x &= 127; x |= 128; x ^= 128; x <<= 1; x >>= 1; x = ~~x; biến p = 1|2&4; x += p-1; nếu (!sai) { trả về x; }; trả về 1; };\n",
        encoding="utf-8")
    combined = cwd / "codegen.vi"
    combined.write_text("\n".join(re.sub(r"(?m)^[ \t]*nhập[^\n]*;[ \t]*$", "", part.read_text(encoding="utf-8"))
                                  for part in [*parts, source]), encoding="utf-8")
    status = execute([jit, "--raw-program", combined], cwd, env, logs, "run", timeout)
    if status or normalize((logs / "run.stdout").read_bytes()) != b"20261017\n" or (logs / "run.stderr").read_bytes():
        raise RuntimeError(f"native JIT codegen failed: {status}")
    obj = cwd / "native-codegen-output.o"
    sections, symbols = parse(obj)
    if len(sections) != 1 or sections[0][:2] != (0, 0) or sections[0][2] <= 0 or sections[0][4] or symbols != [("_main", 15, 0, 0)]:
        raise RuntimeError("unexpected native codegen Mach-O layout")
    # The executable is an output of the linker feature under test, not a
    # compiled test harness. No external assembler or linker participates.
    result = cwd / "native-codegen-result"
    status = execute([jit, "--dynamic", TESTS / "fixtures/native_macho_executable.vi", "|".join(catalog()), result, obj],
                     cwd, env, logs, "link-oracle", max(timeout, 180))
    if status or normalize((logs / "link-oracle.stdout").read_bytes()) != b"Mach-O executable: PASS\n" or (logs / "link-oracle.stderr").read_bytes():
        raise RuntimeError(f"V++ JIT Mach-O link oracle failed: {status}")
    raw = result.read_bytes()
    if struct.unpack_from("<4I", raw) != (0xfeedfacf, 0x100000c, 0, 2):
        raise RuntimeError("invalid executable Mach-O header")
    verify_native_signature(raw)
    result.chmod(0o700)
    if execute(["/usr/bin/codesign", "--verify", result], cwd, env, logs, "signature", 30):
        raise RuntimeError("native codegen executable signature invalid")
    status = execute([result], cwd, env, logs, "machine-code", 30)
    if status != 52 or (logs / "machine-code.stdout").read_bytes() or (logs / "machine-code.stderr").read_bytes():
        raise RuntimeError(f"native codegen machine code returned {status}, expected 52")


def run_case(case, compiler, logs, env, timeout, vm=None, *, phase="both", work=None,
             prepared_binary=None, selector=None, jit=None, interpret=False):
    if interpret and (jit is None or not interpretable(case)):
        raise RuntimeError("unreviewed interpreter regression case")
    # Reject a toolchain case before entering any build/harness branch.
    if jit is not None and (phase == "build" or selector is not None or toolchain_reason(case, jit_mode=True)):
        raise RuntimeError("invalid JIT execution plan")
    logs.mkdir(parents=True, exist_ok=True)
    env = dict(env)
    env.update(case.get("env", {}))
    if case.get("python_fixture"):
        env["VPP_TEST_PYTHON"] = sys.executable
    if env.get("VPP_COMPILER_PROFILE_FILE"):
        env["VPP_COMPILER_PROFILE_FILE"] = str(logs / "compiler.profile")
    if "timezone" in case:
        env["TZ"] = case["timezone"]
    with (tempfile.TemporaryDirectory(prefix="vpp-regression-") if work is None
          else contextlib.nullcontext(work)) as work:
        cwd = Path(work)
        if case.get("hosts_fixture"):
            hosts = cwd / "socket.hosts"
            hosts.write_text("203.0.113.21 fixture.test alias.test\n::1 fixture.test\n"
                             "0:0:0:0:0:0:0:1 fixture.test\n999.1.1.1 fixture.test\n", encoding="ascii")
            env.update(VPP_DNS_VI_HOSTS_FILE=str(hosts), VPP_SOCKET_VI_ONLY="0",
                       VPP_DNS_VI_SERVER="", VPP_DNS_VI_SERVERS="", VPP_DNS_VI_RESOLV_CONF="")
        env["VPP_PREFERENCES_HOME"] = str(cwd / "preferences")
        binary = prepared_binary if prepared_binary is not None else cwd / executable_name("program")
        if case.get("tooling_test"):
            test_work = cwd / "tooling-test-work"
            test_work.mkdir(exist_ok=True)
            env.update(
                VPP_TEST_ROOT=str(ROOT),
                VPP_TEST_WORK=str(test_work),
                VPP_TEST_PYTHON=sys.executable,
                VPP_TEST_CMAKE=shutil.which("cmake") or "cmake",
                VPP_TEST_SELF=str(binary),
            )
            if jit is not None and case["id"] == "kiem_tra_regression_runner":
                env.update(VPP_TEST_SELF=str(jit), VPP_TEST_JIT=str(jit))
        source = ROOT / case["source"]
        if phase == "build" and any(key in case for key in HARNESS_KEYS):
            return True  # These harnesses exercise compilation as part of the test.
        if jit is not None and "dump" in case:
            return jit_dump(case, jit, source, cwd, env, logs, timeout)
        if jit is not None and case.get("cmake") == "checknativeprint.cmake":
            return jit_i32_print(jit, source, cwd, env, logs, timeout)
        if jit is not None and case.get("cmake") in {"checknativelexer.cmake", "checknativeparser.cmake"}:
            return jit_i32_frontend(case, jit, source, cwd, env, logs, timeout)
        if jit is not None and case.get("cmake") == "checknativecodegen.cmake":
            return jit_i32_codegen(jit, source, cwd, env, logs, timeout)
        if jit is not None and case.get("cmake") in JIT_I32_HARNESSES:
            return jit_i32_io(case, jit, source, cwd, env, logs, timeout)
        if "dump" in case:
            for name in ("first", "second"):
                if execute([compiler, case["dump"], source], cwd, env, logs, name, timeout):
                    raise RuntimeError(f"{case['dump']} failed")
            first = (logs / "first.stdout").read_bytes()
            if not first.strip() or first != (logs / "second.stdout").read_bytes():
                raise RuntimeError(f"{case['dump']} produced empty or unstable output")
            return
        if "linker_phase" in case:
            command = [sys.executable, ROOT / "scripts/quality/check_native_regression_linkers.py"]
            if jit is not None:
                command += ["--jit", case["linker_phase"], jit, cwd]
                env = jit_environment(env)
            else:
                command += [case["linker_phase"], compiler, cwd]
            status = execute(command, cwd, env, logs, "harness",
                             max(timeout, case.get("harness_timeout", 3600)))
            if status:
                raise RuntimeError(f"Linker {case['linker_phase']} harness exit {status}")
            return
        if "vm_phase" in case:
            cmake = shutil.which("cmake")
            if not cmake:
                raise RuntimeError("CMake is required for the VM oracle")
            cmd = [cmake, f"-DVPP_ROOT={ROOT}", f"-DVPP_OUTPUT_DIR={cwd}",
                   f"-DVPP_VM_PHASE={case['vm_phase']}"]
            if jit is not None:
                vm = vm or jit.with_name(executable_name("vpp-vm"))
                if not vm.is_file():
                    raise RuntimeError(f"packaged standalone VM missing: {vm}")
                cmd += [f"-DVPP_JIT={jit}", f"-DVPP_PYTHON={sys.executable}"]
                nm = shutil.which("llvm-nm") or shutil.which("nm")
                if nm:
                    cmd.append(f"-DVPP_VM_NM={nm}")
                env = jit_environment(env)
            else:
                cmd += [f"-DVPP_STAGE1={compiler.with_name(executable_name('vpp-i32'))}",
                        f"-DVPP_NATIVE_COMPILER={compiler}"]
            if vm is not None:
                # DNS provider discovery uses the VM's directory. Keep each
                # phase's generated providers beside its own copy of the VM.
                local_vm = cwd / executable_name("vpp-vm")
                shutil.copy2(vm, local_vm)
                cmd.append(f"-DVPP_VM={local_vm}")
            cmd += ["-P", ROOT / "scripts/quality/check_vm_asm.cmake"]
            try:
                status = execute(cmd, cwd, env, logs, "harness", max(timeout, 3600))
            finally:
                for path in cwd.rglob("*"):
                    if path.is_file() and path.suffix in {".out", ".err", ".log", ".stdout", ".stderr", ".status"}:
                        destination = logs / "vm-diagnostics" / path.relative_to(cwd)
                        destination.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(path, destination)
            if status:
                raise RuntimeError(f"Standalone VM {case['vm_phase']} harness exit {status}")
            return
        if jit is not None and case.get("cmake") == "check_native_runtime.cmake":
            cmake = shutil.which("cmake")
            if not cmake:
                raise RuntimeError("CMake is required for the native runtime oracle")
            cmd = [cmake, f"-DVPP_JIT={jit}", f"-DVPP_ROOT={ROOT}",
                   f"-DVPP_TARGET={env.get('VPP_NATIVE_TARGET', 'native')}",
                   f"-DVPP_OUTPUT_DIR={cwd}", "-P", ROOT / "scripts/quality/check_native_runtime.cmake"]
            if execute(cmd, cwd, jit_environment(env), logs, "harness", timeout):
                raise RuntimeError("JIT native runtime harness failed")
            return
        if "cmake" in case:
            cmd = ["cmake", f"-DVPP_EXEC={compiler}", f"-DVPP_STAGE1={compiler}",
                   f"-DVPP_ROOT={ROOT}", f"-DVPP_HOME={env['VPP_HOME']}",
                   f"-DVPP_SOURCE={source}", f"-DVPP_HARNESS={source}",
                   f"-DVPP_KERNEL={ROOT / 'gói/biên dịch/bản địa/phân tích từ vựng.vi'}",
                   f"-DVPP_TARGET={env.get('VPP_NATIVE_TARGET', 'native')}",
                   f"-DVPP_OUTPUT_DIR={cwd}", "-P", ROOT / "scripts/quality" / case["cmake"]]
            status = execute(cmd, cwd, env, logs, "harness", timeout)
            if status:
                raise RuntimeError(f"CMake harness exit {status}")
            return
        if jit is not None:
            original_path = env.get("PATH", "")
            env = jit_environment(env)
            if case["id"] in {"kiem_tra_do_thi_build", "kiem_tra_regression_runner"}:
                # The graph fixture invokes fake shell scripts, never a real compiler.
                env["PATH"] = original_path
            if case.get("compile_reject") or case.get("failure_phase") == "build":
                status = execute([jit, "--check-dynamic", source], cwd, env, logs, "build", timeout)
                if binary.exists():
                    raise RuntimeError("JIT check produced a guest executable")
                if case.get("compile_reject"):
                    if status != 1 or "V++ JIT: từ chối cú pháp".encode() not in (logs / "build.stdout").read_bytes():
                        raise RuntimeError(f"expected JIT syntax rejection, got exit {status}")
                else:
                    verify_failure(case, status, logs, "build")
                return False
        elif phase != "run":
            status = build(compiler, source, binary, ROOT, env, logs, timeout)
            if case.get("compile_reject"):
                if status != 1 or binary.exists():
                    raise RuntimeError(f"malformed source: expected build exit 1 and no executable; got {status}")
                return False
            if status:
                if "diagnostic" in case and case.get("failure_phase") != "run":
                    verify_failure(case, status, logs, "build")
                    return False
                raise RuntimeError(f"build exit {status}")
            if not binary.is_file():
                raise RuntimeError("compiler did not produce executable")
            if case.get("dns_mock"):
                prepare_dns_mock(compiler, cwd, env, logs, timeout)
            if phase == "build":
                return True
        if jit is None and not binary.is_file():
            raise RuntimeError("missing prepared executable")
        if case["suite"] == "full" and not (jit is not None and (
                fixture_free(case, source) or (interpret and interpretable(case)))):
            # Test programs may write relative paths; do not mutate the checkout.
            shutil.copytree(TESTS, cwd / "scripts/test", ignore=shutil.ignore_patterns(".*"))
            shutil.copytree(ROOT / "gói", cwd / "gói")
            shutil.copytree(TESTS / "fixtures", cwd / "fixtures", ignore=shutil.ignore_patterns(".*"))
            if (TESTS / "expected").is_dir():
                shutil.copytree(TESTS / "expected", cwd / "expected")
        if interpret:
            for relative in REVIEWED_RELATIVE_FIXTURES.get(case["id"], {}):
                destination = cwd / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(TESTS / relative, destination)
        if case.get("dns_mock"):
            if jit is not None:
                run_dns_mock([jit, "--dynamic", source], cwd, env, logs,
                             server=[jit, "--raw-program", TESTS / "fixtures/native_dns_mock.vi"],
                             timeout=timeout)
            else:
                run_dns_mock(binary, cwd, env, logs)
            return
        args = case.get("args", []) if selector is None else [selector]
        if jit is not None and case["id"] == "i32-logical-side-effects":
            # The i32 JIT accepts assignment expressions; it has no module loader.
            # Flatten only the declared, function-only local module for this case.
            module, = case["sources"]
            text = source.read_text(encoding="utf-8")
            directive = "nhập native_logical_side_effects.*;"
            if text.count(directive) != 1:
                raise RuntimeError("unexpected i32 fixture import")
            combined = cwd / "logical-side-effects.vi"
            combined.write_text((ROOT / module).read_text(encoding="utf-8") + "\n" + text.replace(directive, "", 1), encoding="utf-8")
            command = [jit, combined]
        elif jit is not None and case["id"] == "i32-logical-call-side-effects":
            # This oracle checks the i32 frontend's short-circuit lowering.
            # The dynamic bytecode frontend has eager logical operators.
            command = [jit, "--raw-program", source, *args]
        elif interpret:
            command = [jit, "--interpret", source]
        else:
            command = [jit, "--dynamic", source, *args] if jit is not None else [binary, *args]
        status = execute(command, cwd, env, logs, "run", timeout)
        if jit is not None:
            if binary.exists():
                raise RuntimeError("JIT produced a guest executable")
            if not (case.get("run_diagnostic_regex") or case.get("diagnostic") or "expected_stderr" in case) and (logs / "run.stderr").read_bytes():
                raise RuntimeError("unexpected JIT stderr")
        if "diagnostic" in case:
            if case.get("failure_phase") == "build":
                raise RuntimeError("expected build rejection, but compiler produced an executable")
            verify_failure(case, status, logs, "run")
            return
        if status != case.get("exit", 0):
            raise RuntimeError(f"run exit {status}, expected {case.get('exit', 0)}")
        if "expected_stderr" in case:
            expected_stderr = (ROOT / case["expected_stderr"]).read_bytes()
            actual_stderr = (logs / "run.stderr").read_bytes()
            if normalize_stderr(actual_stderr) != normalize_stderr(expected_stderr):
                (logs / "expected.stderr").write_bytes(expected_stderr)
                raise RuntimeError("stderr differs from golden")
        if "run_diagnostic_regex" in case:
            stderr = (logs / "run.stderr").read_text(encoding="utf-8", errors="replace")
            if not re.search(case["run_diagnostic_regex"], stderr):
                raise RuntimeError("missing expected runtime diagnostic")
        if "run_forbidden_regex" in case:
            stderr = (logs / "run.stderr").read_text(encoding="utf-8", errors="replace")
            if re.search(case["run_forbidden_regex"], stderr):
                raise RuntimeError("unexpected runtime diagnostic")
        for name in case.get("absent", []):
            if (cwd / name).exists():
                raise RuntimeError(f"test left unexpected file: {name}")
        expected = (ROOT / case["expected"]).read_bytes() if "expected" in case else case["stdout"].encode()
        actual = (logs / "run.stdout").read_bytes()
        if normalize(actual) != normalize(expected):
            (logs / "expected.stdout").write_bytes(expected)
            raise RuntimeError("stdout differs from golden")


def run_dns_mode(server, client, cwd, env, logs, mode, queries, image=None, timeout=30, batch_client=None):
    # Keep the actual V++ server and client on every OS; Python only manages
    # their lifetime and checks the protocol between the test processes.
    gated = image is None and isinstance(client, list) and "--dynamic" in client
    ready_file = logs / "client.ready"
    ready_file.unlink(missing_ok=True)
    config = logs / "config"
    config.write_bytes(f"{mode}\n{-queries if gated else queries}\n".encode("ascii"))
    hosts = logs / "empty-hosts"
    hosts.write_bytes(b"")
    server_out = logs / "server.out"
    deadline = time.monotonic() + timeout
    with config.open("rb") as inp, server_out.open("wb") as out, (logs / "server.err").open("wb") as err:
        worker = subprocess.Popen([str(x) for x in server] if isinstance(server, list) else [str(server)], cwd=cwd, env={**env, "PATH": ""},
                                  stdin=subprocess.PIPE if gated else inp, stdout=out, stderr=err)
        try:
            if gated:
                worker.stdin.write(config.read_bytes())
                worker.stdin.flush()
            while True:
                data = server_out.read_bytes()
                if b"\n" in data:
                    port = data.splitlines()[0].strip()
                    if not port.isdigit() or not 0 < int(port) <= 65535:
                        raise RuntimeError("DNS server returned invalid port")
                    break
                if worker.poll() is not None:
                    raise RuntimeError(f"DNS server exited before readiness: {worker.returncode}")
                if time.monotonic() >= deadline:
                    raise RuntimeError("DNS server readiness timed out")
                time.sleep(0.02)
            client_env = dict(env, VPP_DNS_MOCK_BATCH="0", VPP_DNS_MOCK_MODE=str(mode), VPP_DNS_VI_PORT=port.decode("ascii"),
                              VPP_DNS_VI_SERVER="127.0.0.1", VPP_DNS_VI_SERVERS="127.0.0.2,127.0.0.1" if mode == 4 else "",
                              VPP_DNS_VI_HOSTS_FILE=str(hosts), VPP_DNS_VI_RESOLV_CONF="", VPP_SOCKET_VI_ONLY="1")
            if image is not None:
                client_env["PATH"] = ""
            command = (client if isinstance(client, list) else [client]) if image is None else [client, image]
            if batch_client is not None:
                worker.stdin.write(b"1\n")
                worker.stdin.flush()
                status = batch_client(mode, int(port), logs, deadline)
            elif gated:
                client_env["VPP_DNS_MOCK_READY"] = str(ready_file)
                # Compilation remains bounded by the original harness deadline.
                # The fixture's five-second receive timer starts only when the
                # actual guest is ready. UDP can queue while the gate is released.
                with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
                    task = pool.submit(execute, command, cwd, client_env, logs, "client",
                                       max(0.1, deadline - time.monotonic()))
                    while not ready_file.exists() and not task.done() and time.monotonic() < deadline:
                        time.sleep(0.01)
                    if ready_file.exists():
                        worker.stdin.write(b"1\n")
                        worker.stdin.flush()
                    status = task.result()
                    if not ready_file.exists() and status == 0:
                        raise RuntimeError("DNS client did not enter guest code")
            else:
                status = execute(command, cwd, client_env, logs, "client", max(0.1, deadline - time.monotonic()))
            if image is not None and mode >= 10:
                if status == 0 or b"error=16" not in (logs / "client.stderr").read_bytes():
                    raise RuntimeError(f"VM accepted malformed DNS or returned wrong diagnostic: {status}")
            elif status:
                raise RuntimeError(f"DNS client exit {status}")
            if worker.wait(timeout=max(0.1, deadline - time.monotonic())):
                raise RuntimeError(f"DNS server exit {worker.returncode}")
            if server_out.read_bytes().splitlines()[-1].strip() != str(queries).encode("ascii"):
                raise RuntimeError("DNS server query count mismatch")
            if image is None and normalize((logs / "client.stdout").read_bytes()) != b"VPP DNS MOCK OK\n":
                raise RuntimeError("DNS client output mismatch")
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
            details = [f"DNS mode {mode}: server exit={worker.poll()}; "
                       f"guest readiness={ready_file.exists() if gated else 'not gated'}: {exc}"]
            for name in ("client.ready", "server.out", "server.err", "client.stdout", "client.stderr"):
                path = logs / name
                if path.is_file():
                    with path.open("rb") as stream:
                        stream.seek(max(0, path.stat().st_size - 2048))
                        details.append(f"{name}: {stream.read().decode('utf-8', errors='replace')}")
            raise RuntimeError("\n".join(details)) from exc
        finally:
            if worker.poll() is None:
                worker.kill()
            worker.wait()
            if worker.stdin is not None:
                worker.stdin.close()


def prepare_dns_mock(compiler, cwd, env, logs, timeout):
    i32 = compiler.with_name(executable_name("vpp-i32"))
    i32_env = dict(env)
    if platform.system() == "Linux":
        i32_env["VPP_NATIVE_ELF_LINK"] = "1"
    server = cwd / executable_name("dns-server")
    server_logs = logs / "server-build"
    server_logs.mkdir(exist_ok=True)
    if platform.system() == "Windows":
        from check_windows_full_bootstrap import raw_build
        raw_build(i32, TESTS / "fixtures/native_dns_mock.vi", server, i32_env, server_logs, timeout=timeout)
    elif build(i32, TESTS / "fixtures/native_dns_mock.vi", server, ROOT, i32_env, server_logs, timeout):
        raise RuntimeError("DNS server build failed")
    if not server.is_file():
        raise RuntimeError("missing DNS server executable")


@contextlib.contextmanager
def dns_batch_client(client, cwd, env, logs, deadline):
    """Compile the explicit-port DNS cases once, then send one mode at a time."""
    batch_logs = logs / "client-batch"
    batch_logs.mkdir(exist_ok=True)
    hosts = batch_logs / "empty-hosts"
    hosts.write_bytes(b"")
    client_env = dict(env, VPP_DNS_MOCK_BATCH="1", VPP_DNS_MOCK_READY="",
                      VPP_DNS_VI_SERVER="127.0.0.1", VPP_DNS_VI_SERVERS="",
                      VPP_DNS_VI_HOSTS_FILE=str(hosts), VPP_DNS_VI_RESOLV_CONF="", VPP_SOCKET_VI_ONLY="1")
    command, client_env = fixture_process(client, client_env)
    stdout = batch_logs / "client.stdout"
    stderr = batch_logs / "client.stderr"
    with stdout.open("wb") as out, stderr.open("wb") as err, stdout.open("rb") as response:
        process = subprocess.Popen(command, cwd=cwd, env=client_env, stdin=subprocess.PIPE,
                                   stdout=out, stderr=err, start_new_session=(os.name != "nt"))

        def read_line(until):
            while True:
                offset = response.tell()
                line = response.readline()
                if line.endswith(b"\n"):
                    return normalize(line)
                response.seek(offset)
                if process.poll() is not None:
                    raise RuntimeError(f"DNS batch guest exited {process.returncode}: {stderr.read_bytes()[-4096:]!r}; stdout={line!r}")
                if time.monotonic() >= until:
                    raise RuntimeError("DNS batch guest timed out")
                time.sleep(0.01)

        def run_mode(mode, port, mode_logs, until):
            start = time.monotonic()
            process.stdin.write(f"{mode}\n{port}\n".encode("ascii"))
            process.stdin.flush()
            line = read_line(min(until, deadline))
            (mode_logs / "client.stdout").write_bytes(line)
            (mode_logs / "client.seconds").write_text(f"{time.monotonic() - start:.2f}")
            if line != b"VPP DNS MOCK OK\n":
                raise RuntimeError(f"DNS batch mode {mode}: unexpected output {line!r}")
            return 0

        try:
            if read_line(deadline) != b"VPP DNS MOCK READY\n":
                raise RuntimeError("DNS batch guest failed to enter main")
            yield run_mode
            process.stdin.write(b"-1\n")
            process.stdin.flush()
            if process.wait(timeout=max(0.1, deadline - time.monotonic())):
                raise RuntimeError(f"DNS batch guest exit {process.returncode}")
            if response.read() or stderr.read_bytes():
                raise RuntimeError("DNS batch guest produced unexpected trailing output")
        finally:
            if process.poll() is None:
                if os.name == "nt":
                    process.kill()
                else:
                    os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            process.stdin.close()


def run_dns_mock(client, cwd, env, logs, server=None, timeout=30):
    if server is None:
        server = cwd / executable_name("dns-server")
    modes = (0, 1, 2, 3, 4, 10, 11, 12, 13, 14, 15, 16, 17, 20, 21, 30, 31, 32, 33, 34, 35, 36, 37)
    if isinstance(client, list) and "--dynamic" in client:
        deadline = time.monotonic() + timeout
        with dns_batch_client(client, cwd, env, logs, deadline) as run_mode:
            for mode in modes:
                if mode == 3:
                    continue
                queries = 2 if mode in (0, 1, 20, 21) else 1
                mode_logs = logs / f"mode-{mode}"
                mode_logs.mkdir(exist_ok=True)
                try:
                    run_dns_mode(server, client, cwd, env, mode_logs, mode, queries,
                                 timeout=min(30, max(0.1, deadline - time.monotonic())), batch_client=run_mode)
                except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
                    raise RuntimeError(f"DNS mock mode {mode}: {exc}") from exc
        # The environment-based resolver has its own fresh guest, preserving
        # the original test of VPP_DNS_VI_PORT and the hosts-file override.
        mode_logs = logs / "mode-3"
        mode_logs.mkdir(exist_ok=True)
        run_dns_mode(server, client, cwd, env, mode_logs, 3, 2,
                     timeout=max(0.1, deadline - time.monotonic()))
        return
    failures = []
    for mode in modes:
        queries = 2 if mode in (0, 1, 3, 20, 21) else 1
        mode_logs = logs / f"mode-{mode}"
        mode_logs.mkdir(exist_ok=True)
        try:
            run_dns_mode(server, client, cwd, env, mode_logs, mode, queries, timeout=timeout)
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
            failures.append(f"mode {mode}: {exc}")
    if failures:
        raise RuntimeError("DNS mock: " + "; ".join(failures))


def prepare_http(compiler, directory, env, timeout):
    directory.mkdir(parents=True, exist_ok=True)
    binary = directory / executable_name("server")
    if build(compiler, TESTS / "http_fixture.vi", binary, ROOT, env, directory, timeout):
        raise RuntimeError("HTTP fixture failed to compile")
    return binary


def start_prepared_http(binary, directory, env, timeout=30):
    directory.mkdir(parents=True, exist_ok=True)
    command = [str(x) for x in binary] if isinstance(binary, list) else [str(binary)]
    original = command
    command, env = fixture_process(command, env)
    # Only the verified process double uses an ephemeral port. Real V++
    # HTTP clients retain the existing fixed-port protocol.
    mock_server = command != original
    port_file = directory / "server.port"
    if mock_server:
        port_file.unlink(missing_ok=True)
    log_path = directory / "server.log"
    with log_path.open("wb") as log:
        process = subprocess.Popen(command, cwd=directory, env=env, stdout=log, stderr=log)
    port = None if mock_server else 18080
    last_error = "waiting for fixture startup"
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and process.poll() is None:
            try:
                if mock_server:
                    published_port = int(port_file.read_text(encoding="ascii"))
                    if not 0 < published_port <= 65535:
                        raise ValueError("invalid fixture port")
                    port = published_port
                with opener.open(f"http://127.0.0.1:{port}/health", timeout=1) as response:
                    if json.loads(response.read()) == {"ok": True} and process.poll() is None:
                        return process
                    last_error = "unexpected health response or fixture exited"
            except (OSError, ValueError) as exc:
                last_error = f"{type(exc).__name__}: {exc}"
            time.sleep(0.2)
        detail = log_path.read_bytes()[-4096:].decode("utf-8", errors="replace").strip()
        endpoint = f"localhost:{port}" if port is not None else f"unpublished port ({port_file})"
        kind = "mock" if mock_server else "V++"
        raise RuntimeError(f"HTTP fixture not ready on {endpoint}; exit={process.poll()}; "
                           f"fixture={kind}; last error={last_error}; {detail}")
    except BaseException:
        stop_http(process)
        raise


def start_http(compiler, directory, env, timeout):
    return start_prepared_http(prepare_http(compiler, directory, env, timeout), directory, env)


def stop_http(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def staged_regression(cases, compiler, logs, env, timeout, jobs, record, batch_size=1,
                      module_suites=False, artifacts=None):
    """Build each fixture once, then execute prepared binaries in parallel.

    Compiler/linker/VM harnesses intentionally compile their own inputs as
    part of their assertions. The ordinary AOT run phase never calls build.
    """
    errors = (OSError, RuntimeError, AssertionError, struct.error, subprocess.TimeoutExpired)
    vm = None
    http_binary = None
    setup_errors = {}
    http_dir = (artifacts if artifacts is not None else logs) / "http-fixture"
    if any("vm_phase" in c for c in cases):
        try:
            vm = prepare_vm(compiler, logs, env, timeout)
        except errors as exc:
            setup_errors["vm_phase"] = str(exc)
    if any(c.get("http") for c in cases):
        try:
            http_binary = prepare_http(compiler, http_dir, env, timeout)
        except errors as exc:
            setup_errors["http"] = str(exc)
    elapsed = {}
    http_lock = threading.Lock()
    # A persistent build artifact can be extracted elsewhere and run repeatedly.
    # Normal combined runs keep executables off the diagnostics artifact.
    with (tempfile.TemporaryDirectory(prefix="vpp-regression-build-") if artifacts is None
          else contextlib.nullcontext(artifacts / "work")) as work:
        work = Path(work)
        work.mkdir(parents=True, exist_ok=True)
        directories = {c["id"]: work / str(i) for i, c in enumerate(cases)}
        for directory in directories.values():
            directory.mkdir()
        suites = plan_module_suites([c for c in cases if not any(k in c for k in setup_errors)]) if module_suites else {}
        prepared = {}
        for name, members in suites.items():
            binary = work / executable_name("module-" + name)
            for index, (case, _) in enumerate(members):
                prepared[case["id"]] = (binary, str(index))

        def stage(case, phase):
            name = case["id"]
            start = time.monotonic()
            label = ("CHECK" if artifacts is not None else "DEFER HARNESS") if phase == "build" and any(k in case for k in HARNESS_KEYS) else phase.upper()
            print(f"{label} {name}", flush=True)
            try:
                for key, detail in setup_errors.items():
                    if key in case:
                        raise RuntimeError(detail)
                # The HTTP fixture serves a fixed port and may have mutable
                # state; serialize its clients while other cases run freely.
                lock = http_lock if phase == "run" and case.get("http") else contextlib.nullcontext()
                with lock:
                    options = {}
                    if name in prepared:
                        options["prepared_binary"], options["selector"] = prepared[name]
                    # Compiler/linker/VM checks exercise builds as assertions.
                    # Finish these checks in the build job, never the run job.
                    check = artifacts is not None and phase == "build" and any(k in case for k in HARNESS_KEYS)
                    ready = run_case(case, compiler, logs / name, env, timeout,
                                     vm=vm, phase="both" if check else phase,
                                     work=directories[name], **options)
                    if check:
                        ready = False
                return case, bool(ready), "", time.monotonic() - start
            except errors as exc:
                return case, False, str(exc), time.monotonic() - start

        def build_group(group, members):
            if len(members) == 1:
                return [stage(members[0], "build")]
            print(f"BUILD BATCH {group}: " + ", ".join(c["id"] for c in members), flush=True)
            completed, failure = build_batch(members, compiler, directories, logs, env, timeout, group)
            results = []
            for case in members:
                name = case["id"]
                if name in completed:
                    results.append((case, True, "", completed[name]))
                else:
                    results.append((case, False, f"{failure}; batch {group}", 0))
            return results

        def build_suite(name, members):
            start = time.monotonic()
            module_logs = logs / "modules" / name
            failure = ""
            try:
                build_module_suite(name, members, compiler,
                                   prepared[members[0][0]["id"]][0], module_logs, env, timeout)
            except errors as exc:
                failure = str(exc)
            seconds = time.monotonic() - start
            results = []
            for case, _ in members:
                directory = logs / case["id"]
                directory.mkdir(exist_ok=True)
                (directory / "build.module").write_text(f"modules/{name}\n")
                (directory / "build.status").write_text("1" if failure else "0")
                if failure:
                    for suffix in ("stdout", "stderr"):
                        source = module_logs / f"build.{suffix}"
                        if source.is_file():
                            shutil.copyfile(source, directory / f"build.{suffix}")
                results.append((case, not failure, failure, seconds / len(members)))
            return results

        groups = []
        eligible = []
        for case in cases:
            if case["id"] in prepared:
                continue
            if batch_size > 1 and batch_eligible(case) and not any(k in case for k in setup_errors):
                eligible.append(case)
                if len(eligible) == batch_size:
                    groups.append(eligible)
                    eligible = []
            else:
                groups.append([case])
        if eligible:
            groups.append(eligible)
        if suites:
            print(f"PLAN: {len(prepared)} cases in {len(suites)} module executables; "
                  f"{len(cases) - len(prepared)} independent/batch/harness cases", flush=True)
        ready = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = [pool.submit(build_suite, name, members) for name, members in suites.items()]
            futures += [pool.submit(build_group, i, group) for i, group in enumerate(groups)]
            for completed, future in enumerate(concurrent.futures.as_completed(futures), 1):
                group_results = future.result()
                print(f"BUILD PROGRESS {completed}/{len(futures)} groups: " +
                      ", ".join(f"{c['id']} ({seconds:.2f}s)" for c, _, _, seconds in group_results),
                      flush=True)
                for case, needs_run, detail, seconds in group_results:
                    elapsed[case["id"]] = seconds
                    if detail or not needs_run:
                        record(case, "FAIL" if detail else "PASS", detail, seconds)
                        shutil.rmtree(directories[case["id"]])
                    else:
                        ready.append(case)
        print(f"BUILD PHASE COMPLETE: {len(ready)} cases ready for execution", flush=True)
        if artifacts is not None:
            publications = []
            for case in ready:
                name = case["id"]
                binary, selector = prepared.get(name, (directories[name] / executable_name("program"), None))
                publications.append({"id": name,
                                     "directory": directories[name].relative_to(artifacts).as_posix(),
                                     "binary": binary.relative_to(artifacts).as_posix(),
                                     "selector": selector})
            return {
                "ready": publications,
                "http": http_binary.relative_to(artifacts).as_posix() if http_binary is not None else None,
            }
        server = None
        try:
            # This tooling fixture starts its own runner/server on port 18080.
            # Execute it before starting our shared HTTP server, even on a
            # single shard, to avoid a collision with the nested fixture.
            isolated = [c for c in ready if c["id"] == "kiem_tra_regression_runner"]
            for c in isolated:
                case, _, detail, seconds = stage(c, "run")
                record(case, "FAIL" if detail else "PASS", detail, elapsed[case["id"]] + seconds)
                shutil.rmtree(directories[case["id"]])
                ready.remove(c)
            if http_binary is not None:
                try:
                    server = start_prepared_http(http_binary, http_dir, env)
                except errors as exc:
                    setup_errors["http"] = str(exc)
            with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
                for future in concurrent.futures.as_completed(
                        [pool.submit(stage, c, "run") for c in ready]):
                    case, _, detail, seconds = future.result()
                    record(case, "FAIL" if detail else "PASS", detail,
                           elapsed[case["id"]] + seconds)
                    shutil.rmtree(directories[case["id"]])
        finally:
            stop_http(server)


def artifact_inputs(compiler, jit=None):
    """Reject artifacts from a different compiler, test corpus or tool version."""
    digest = hashlib.sha256()
    paths = [MANIFEST, Path(__file__).resolve()]
    paths += [p for root in (ROOT / "gói", TESTS) for p in root.rglob("*")
              if p.is_file() and p.suffix in (".vi", ".expected")
              and not any(part.startswith(".") for part in p.relative_to(ROOT).parts)]
    for path in sorted(paths):
        digest.update(path.relative_to(ROOT).as_posix().encode("utf-8") + b"\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    backend = compiler.with_name(executable_name("vpp-native"))
    return {"version": 1, "system": platform.system(), "machine": platform.machine().lower(),
            "compiler": hashlib.sha256(compiler.read_bytes()).hexdigest(),
            "backend": hashlib.sha256(backend.read_bytes()).hexdigest() if backend.is_file() else None,
            "sources": digest.hexdigest(),
            "jit": hashlib.sha256(jit.read_bytes()).hexdigest() if jit is not None else None}


def run_prepared_regression(cases, compiler, artifacts, plan, logs, env, timeout, record, jit=None):
    """Run saved AOT binaries and JIT sources in fresh workspaces."""
    ready = {c["id"]: c for c in plan["ready"]}
    checked = {c["id"]: c for c in plan["checks"]}
    jit_ids = set(plan.get("jit_cases", []))
    selected_jit = {c["id"] for c in cases if jit is not None and c.get("jit")}
    if jit_ids.intersection(c["id"] for c in cases) != selected_jit or jit_ids & (ready.keys() | checked.keys()):
        raise RuntimeError("JIT cases do not match the test artifact")
    missing = {c["id"] for c in cases} - (ready.keys() | checked.keys() | jit_ids)
    if missing:
        raise RuntimeError(f"cases missing from build artifact: {sorted(missing)}")

    def artifact_file(relative):
        path = (artifacts / relative).resolve()
        if not path.is_relative_to(artifacts) or not path.is_file():
            raise RuntimeError(f"missing artifact file: {relative}; rebuild the test artifact")
        return path

    # Fail before running anything if an executable/helper is absent. Never
    # fall back to compilation, even when the compiler is available.
    for case in cases:
        if case["id"] in ready:
            item = ready[case["id"]]
            artifact_file(item["binary"])
            if case.get("dns_mock"):
                artifact_file(item["directory"] + "/" + executable_name("dns-server"))
    http_binary = artifact_file(plan["http"]) if any(c.get("http") and c["id"] in ready for c in cases) else None
    errors = (OSError, RuntimeError, AssertionError, struct.error, subprocess.TimeoutExpired)
    server = None
    http_error = None
    # The nested runner starts its own HTTP server on the shared port.
    cases = sorted(cases, key=lambda c: c["id"] != "kiem_tra_regression_runner")
    with tempfile.TemporaryDirectory(prefix="vpp-regression-run-") as temporary:
        work = Path(temporary)
        try:
            for index, case in enumerate(cases):
                name = case["id"]
                if name in checked:
                    result = checked[name]
                    record(case, result["status"], "checked during build: " + result["detail"], 0)
                    continue
                start = time.monotonic()
                print(f"RUN {name} ({'JIT' if name in jit_ids else 'prebuilt'})", flush=True)
                case_work = work / str(index)
                try:
                    if name in jit_ids:
                        try:
                            run_case(case, compiler, logs / name, env, timeout, phase="run", jit=jit)
                            status, detail = "PASS", "JIT"
                        except errors as exc:
                            status, detail = "FAIL", f"JIT: {exc}"
                        record(case, status, detail, time.monotonic() - start)
                        continue
                    if case.get("http") and server is None:
                        if http_error:
                            raise RuntimeError(http_error)
                        directory = work / "http-fixture"
                        directory.mkdir(exist_ok=True)
                        binary = directory / executable_name("server")
                        shutil.copy2(http_binary, binary)
                        try:
                            server = start_prepared_http(binary, directory, env)
                        except errors as exc:
                            http_error = str(exc)
                            raise
                    item = ready[name]
                    directory = case_work
                    shutil.copytree(artifacts / item["directory"], directory)
                    binary = directory / executable_name("program")
                    # Shared module executables live above the case workspace.
                    shutil.copy2(artifact_file(item["binary"]), binary)
                    run_case(case, compiler, logs / name, env, timeout, phase="run",
                             work=directory, prepared_binary=binary, selector=item["selector"])
                    status, detail = "PASS", ""
                except errors as exc:
                    status, detail = "FAIL", str(exc)
                finally:
                    shutil.rmtree(case_work, ignore_errors=True)
                record(case, status, detail, time.monotonic() - start)
        finally:
            stop_http(server)
            http_log = work / "http-fixture/server.log"
            if http_log.is_file():
                destination = logs / "http-fixture"
                destination.mkdir(exist_ok=True)
                shutil.copy2(http_log, destination / "server.log")


def direct_regression(cases, compiler, jit, logs, env, timeout, jobs, record, *, jit_only=False, vm=None,
                      interpret_reviewed=False):
    """Run sources independently; never build or fall back for a JIT case."""
    errors = (OSError, RuntimeError, AssertionError, struct.error, subprocess.TimeoutExpired)
    guests = [c for c in cases if jit_eligible(c, jit_only)]
    checks = [c for c in cases if not jit_eligible(c, jit_only)]
    interpret_ids = {c["id"] for c in guests if interpret_reviewed and interpretable(c)}
    if jit_only:
        print(f"PLAN: {len(guests)} direct JIT cases, {len(checks)} cases requiring JIT migration; no builds", flush=True)
        for case in checks:
            record(case, "FAIL", "JIT migration required: " + toolchain_reason(case, jit_mode=True), 0)
        checks = []
    else:
        print(f"PLAN: {len(guests)} direct JIT cases, {len(checks)} AOT/toolchain cases", flush=True)
    if interpret_reviewed:
        print(f"PLAN: {len(interpret_ids)} reviewed AST interpreter cases", flush=True)
    server = None
    def run_guest(case):
        start = time.monotonic()
        interpret = case["id"] in interpret_ids
        print(f"START {'INTERPRET' if interpret else 'JIT'} {case['id']}", flush=True)
        try:
            run_case(case, compiler, logs / case["id"], env, timeout, jit=jit, vm=vm,
                     interpret=interpret)
            return case, "PASS", "", time.monotonic() - start
        except errors as exc:
            return case, "FAIL", str(exc), time.monotonic() - start
    # HTTP has a fixed port. Keep the shared source server isolated from
    # other fixtures, especially tooling tests which start their own server.
    ordinary = [c for c in guests if not c.get("http")]
    http = [c for c in guests if c.get("http")]
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for result in concurrent.futures.as_completed([pool.submit(run_guest, c) for c in ordinary]):
            record(*result.result())
    if http:
        try:
            server = start_prepared_http([jit, "--dynamic", TESTS / "http_fixture.vi"],
                                         logs / "http-fixture", jit_environment(env), timeout)
            for c in http:
                record(*run_guest(c))
        except errors as exc:
            for c in http:
                record(c, "FAIL", f"JIT HTTP fixture: {exc}", 0)
        finally:
            stop_http(server)
    if checks:
        # Build unqualified fixtures once, then execute them without invoking
        # the compiler again. A failing qualified JIT case never enters here.
        staged_regression(checks, compiler, logs, env, timeout, jobs, record)


def print_failure_logs(directory):
    for name in ("build.stderr", "run.stderr", "run.stdout", "harness.stderr", "harness.stdout"):
        path = directory / name
        if path.is_file():
            with path.open("rb") as stream:
                stream.seek(max(0, path.stat().st_size - 4096))
                tail = stream.read().decode("utf-8", errors="replace").strip()
            if tail:
                print(f"  {name}: {tail}", flush=True)


def main():
    global FIXTURE_EXECUTABLE
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture-executable", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--compiler", type=Path)
    parser.add_argument("--backend", choices=("jit", "aot", "toolchain", "mixed"),
                        help="jit: source-only, no builds; toolchain: only backend-specific checks; aot: all native checks; mixed: qualified JIT + AOT; default: JIT for all source suites, mixed/AOT for artifacts")
    parser.add_argument("--interpret-reviewed", action="store_true",
                        help="evaluate hash-pinned, golden-verified simple JIT fixtures directly as AST")
    parser.add_argument("--jit", type=Path,
                        help="path to packaged JIT (default: sibling of compiler or build-pure-vpp/bin/vpp-jit)")
    parser.add_argument("--vm", type=Path, help="packaged standalone VM (default: beside JIT)")
    parser.add_argument("--suite", choices=("full", "i32", "smoke", "host", "guest"), default="full")
    parser.add_argument("--case", action="append")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--inventory", action="store_true")
    parser.add_argument("--include-manual", action="store_true", help="include tests requiring public network/services; --case also opts in")
    parser.add_argument("--profile", action="store_true", help="save a separate compiler.profile for each case")
    parser.add_argument("--shard-count", type=int, default=1)
    parser.add_argument("--shard-index", type=int, default=0)
    parser.add_argument("--jobs", type=int, help="number of concurrent regression workers")
    parser.add_argument("--batch-size", type=int, choices=range(1, 5), default=1,
                        help="experimental full compiler AST reuse, up to four builds per process")
    parser.add_argument("--module-suites", action="store_true",
                        help="compile reviewed full regression cases into shared executables")
    parser.add_argument("--phase", choices=("all", "build", "run"), default="all",
                        help="build a reusable test artifact, or run it without compiling")
    parser.add_argument("--artifacts", type=Path,
                        help="test artifact directory for --phase build or --phase run")
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--logs", type=Path, default=ROOT / "build-pure-vpp/regression-logs")
    args = parser.parse_args()
    if args.fixture_executable is not None:
        if (os.environ.get("VPP_TEST_FAKE") != "1" or
                not (ROOT / "scripts/quality/regression_runner_mock.py").is_file()):
            parser.error("--fixture-executable is reserved for the isolated runner self-test")
        FIXTURE_EXECUTABLE = args.fixture_executable.resolve()
        if not FIXTURE_EXECUTABLE.is_file():
            parser.error("runner fixture JIT is missing")
    if args.backend is None:
        if args.suite == "host":
            args.backend = "aot"
        else:
            args.backend = "jit" if args.phase == "all" else ("mixed" if args.suite != "i32" else "aot")
    if args.suite == "host" and args.backend != "aot":
        parser.error("trusted-host suite requires --backend aot")
    if args.suite == "guest" and args.backend != "jit":
        parser.error("dynamic-guest suite requires --backend jit")
    manifest = load_manifest()
    cases = [c for c in manifest["cases"] if (c["suite"] == args.suite or
             (args.suite == "smoke" and c.get("smoke")) or
             (args.suite == "full" and args.backend == "jit" and c["suite"] in ("i32", "guest")))]
    if args.case:
        unknown = set(args.case) - {c["id"] for c in cases}
        if unknown:
            parser.error(f"unknown case(s): {sorted(unknown)}")
        cases = [c for c in cases if c["id"] in args.case]
    if args.backend == "toolchain":
        cases = [c for c in cases if toolchain_reason(c)]
    if args.inventory:
        print(collections.Counter(c["suite"] + (" pending" if "pending" in c else " active") for c in manifest["cases"]))
        print(f"Unregistered source files: {len(manifest['unregistered_sources'])}")
    if args.list:
        for c in cases:
            label = (" [PENDING: " + c["pending"] + "]" if "pending" in c else "")
            if c.get("platforms"):
                label += " [" + ", ".join(c["platforms"]) + "]"
            if c.get("manual"):
                label += " [MANUAL: " + c["manual"] + "]"
            print(c["id"] + label)
        return 0
    if args.inventory:
        return 0
    if not 0 <= args.shard_index < args.shard_count or args.timeout < 1:
        parser.error("invalid shard or timeout")
    if args.jobs is not None and args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.batch_size > 1 and (args.suite != "full" or args.jobs is None):
        parser.error("--batch-size requires the full suite and --jobs")
    if args.module_suites and (args.suite != "full" or args.jobs is None):
        parser.error("--module-suites requires the full suite and --jobs")
    if (args.phase != "all") != (args.artifacts is not None):
        parser.error("--phase build/run requires --artifacts; --phase all does not accept it")
    direct = args.phase == "all" and args.backend not in ("aot", "toolchain") and not (args.backend == "mixed" and args.suite == "i32")
    if direct and (args.batch_size != 1 or args.module_suites):
        parser.error("AOT batching/module suites require --backend aot")
    if args.backend == "jit" and args.phase != "all":
        parser.error("direct JIT does not prepare test artifacts")
    if args.backend == "toolchain" and (args.phase != "all" or args.jit is not None):
        parser.error("toolchain checks require --phase all and no --jit")
    if args.interpret_reviewed and (args.backend != "jit" or args.phase != "all"):
        parser.error("--interpret-reviewed requires --backend jit and --phase all")
    jit = args.jit.resolve() if args.jit is not None else None
    if jit is not None and not jit.is_file():
        parser.error(f"missing JIT: {jit}")
    if args.backend == "jit" and jit is None:
        jit = (args.compiler.resolve().with_name(executable_name("vpp-jit")) if args.compiler else
               ROOT / "build-pure-vpp/bin" / executable_name("vpp-jit"))
    if args.compiler is None and args.backend != "jit":
        parser.error("--compiler required")
    compiler = args.compiler.resolve() if args.compiler is not None else None
    if args.backend != "jit" and compiler is not None and not compiler.is_file():
        parser.error(f"missing compiler: {compiler}")
    if direct and jit is None:
        jit = compiler.with_name(executable_name("vpp-jit"))
    if direct and not jit.is_file():
        parser.error(f"missing JIT: {jit}")
    if args.backend == "jit" and args.shard_count != 1:
        parser.error("JIT regression must run as one suite")
    if args.shard_count > 1:
        shards = partition(cases, args.shard_count, args.module_suites)
        cases = shards[args.shard_index]
        print(f"SHARD {args.shard_index + 1}/{args.shard_count}: {len(cases)} registered cases", flush=True)
    else:
        print(f"SUITE {args.suite}: {len(cases)} registered cases", flush=True)
    host = platform.system()
    quiet_platform_skips = host in ("Linux", "Windows")
    pending = [c for c in cases if "pending" in c]
    unsupported = [c for c in cases if "pending" not in c and c.get("platforms") and host not in c["platforms"]]
    manual = [c for c in cases if c.get("manual") and c not in pending and c not in unsupported
              and not (args.include_manual or args.case)]
    if not quiet_platform_skips:
        for c in unsupported:
            print(f"UNSUPPORTED {c['id']} on {host}", flush=True)
    if not cases:
        parser.error("no cases in selection")
    cases = [c for c in cases if c not in pending and c not in unsupported and c not in manual]
    artifacts = args.artifacts.resolve() if args.artifacts is not None else None
    plan = None
    if args.phase == "build":
        if artifacts.exists() and any(artifacts.iterdir()):
            parser.error(f"test artifact directory must be empty: {artifacts}")
        artifacts.mkdir(parents=True, exist_ok=True)
    elif args.phase == "run":
        try:
            plan = json.loads((artifacts / "build.json").read_text(encoding="utf-8"))
            if plan["inputs"] != artifact_inputs(compiler, jit) or plan["suite"] != args.suite:
                parser.error("test artifact does not match this compiler, JIT, sources, suite or platform; rebuild it")
        except (OSError, ValueError, KeyError) as exc:
            parser.error(f"invalid test artifact: {exc}")
    env = dict(os.environ)
    # SQLite is a runtime dependency of the database driver, not a compiler.
    # Resolve it before JIT isolation strips PATH; retain the compiler guards.
    if not env.get("VPP_SQLITE3"):
        sqlite = shutil.which("sqlite3", path=env.get("PATH"))
        if sqlite:
            env["VPP_SQLITE3"] = str(Path(sqlite).resolve())
    installed = (jit if args.backend == "jit" else compiler).parent.parent / "share/vpp"
    env.setdefault("VPP_HOME", str(installed if (installed / "gói").is_dir() else ROOT))
    env.update(VPP_NATIVE_NO_CACHE="1", VPP_FFI_ENV_TEST="vpp-env-ffi-ok", VPP_FFI_ENV_EMPTY_TEST="")
    # Child quality scripts write UTF-8 logs even when Windows redirects stdout.
    env["PYTHONIOENCODING"] = "utf-8"
    env.pop("VPP_FFI_ENV_MISSING_TEST", None)
    logs = args.logs.resolve()
    logs.mkdir(parents=True, exist_ok=True)
    logs = Path(tempfile.mkdtemp(prefix="run-", dir=logs))
    if args.profile:
        env["VPP_COMPILER_PROFILE_FILE"] = str(logs / "compiler.profile")
    server = None
    http_error = None
    vm = None
    vm_error = None
    results = [{"id": c["id"], "status": "PENDING", "detail": c["pending"], "seconds": 0} for c in pending]
    # Preserve platform exclusions in the machine-readable report, even when
    # they are omitted from the Linux/Windows console output.
    results += [{"id": c["id"], "status": "UNSUPPORTED", "detail": host, "seconds": 0} for c in unsupported]
    results += [{"id": c["id"], "status": "MANUAL", "detail": c["manual"], "seconds": 0} for c in manual]
    for c in manual:
        print(f"MANUAL {c['id']}: {c['manual']} (--include-manual or --case to run)", flush=True)
    for c in pending:
        print(f"PENDING {c['id']}: {c['pending']}", flush=True)
    results_path = logs / "results.json"
    results_path.write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
    def record(case, status, detail, seconds):
        directory = logs / case["id"]
        seconds = round(seconds, 2)
        stages = {p.stem: float(p.read_text()) for p in directory.glob("*.seconds")}
        results.append({"id": case["id"], "status": status, "detail": detail,
                        "seconds": seconds, "stages": stages})
        results_path.write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
        timing = ", ".join(f"{name}={value:.2f}s" for name, value in sorted(stages.items()))
        print(f"{status} {case['id']} ({seconds:.2f}s; {timing}) {detail}", flush=True)
        if status == "FAIL":
            print_failure_logs(directory)

    if args.phase == "run":
        try:
            run_prepared_regression(cases, compiler, artifacts, plan, logs, env, args.timeout, record, jit=jit)
        except (OSError, RuntimeError, KeyError, TypeError) as exc:
            parser.error(f"cannot run test artifact: {exc}")
    elif args.phase == "build":
        jit_cases = [c["id"] for c in cases if jit is not None and c.get("jit")]
        aot_cases = [c for c in cases if c["id"] not in jit_cases]
        print(f"PLAN: {len(jit_cases)} JIT cases, {len(aot_cases)} AOT/harness cases", flush=True)
        plan = staged_regression(aot_cases, compiler, logs, env, args.timeout, args.jobs or 1, record,
                                 batch_size=args.batch_size, module_suites=args.module_suites,
                                 artifacts=artifacts)
        plan.update(inputs=artifact_inputs(compiler, jit), suite=args.suite, jit_cases=jit_cases,
                    checks=[r for r in results if r["status"] in ("PASS", "FAIL")])
        (artifacts / "build.json").write_text(json.dumps(plan, ensure_ascii=False, indent=2), encoding="utf-8")
        ready_ids = {c["id"] for c in plan["ready"]}
        for case in cases:
            if case["id"] in ready_ids:
                record(case, "BUILT", "saved for --phase run", 0)
            elif case["id"] in jit_cases:
                record(case, "JIT_READY", "source saved in checkout; run with packaged JIT", 0)
    elif direct:
        direct_regression(cases, compiler, jit, logs, env, args.timeout, args.jobs or 1, record,
                          jit_only=args.backend == "jit", vm=args.vm.resolve() if args.vm else None,
                          interpret_reviewed=args.interpret_reviewed)
    elif args.jobs is not None:
        staged_regression(cases, compiler, logs, env, args.timeout, args.jobs, record,
                          batch_size=args.batch_size, module_suites=args.module_suites)
    else:
        try:
            for case in cases:
                start = time.monotonic()
                directory = logs / case["id"]
                print(f"START {case['id']}", flush=True)
                try:
                    if "vm_phase" in case and vm is None:
                        if vm_error:
                            raise RuntimeError(vm_error)
                        try:
                            vm = prepare_vm(compiler, logs, env, args.timeout)
                        except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
                            vm_error = str(exc)
                            raise
                    if case["id"] == "kiem_tra_regression_runner" and server is not None:
                        stop_http(server)
                        server = None
                    if case.get("http") and server is None:
                        if http_error:
                            raise RuntimeError(http_error)
                        try:
                            server = start_http(compiler, logs / "http-fixture", env, args.timeout)
                        except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
                            http_error = str(exc)
                            raise
                    run_case(case, compiler, directory, env, args.timeout, vm=vm,
                             jit=jit if case.get("jit") and args.backend != "aot" else None)
                    status, detail = "PASS", ""
                except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
                    status, detail = "FAIL", str(exc)
                seconds = round(time.monotonic() - start, 2)
                record(case, status, detail, seconds)
        finally:
            stop_http(server)
            results_path.write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
    failed = sum(c["status"] == "FAIL" for c in results)
    passed = sum(c["status"] == "PASS" for c in results)
    summary = f"{passed} passed, {failed} failed, {len(pending)} pending"
    if manual:
        summary += f", {len(manual)} manual (not run)"
    if args.phase == "build":
        summary = f"{len(plan['ready'])} built, {len(plan['jit_cases'])} JIT ready, {passed} build checks passed, {failed} failed, {len(pending)} pending"
    if not quiet_platform_skips:
        summary += f", {len(unsupported)} unsupported on this platform"
    print(f"{summary}; logs: {logs}")
    return int(failed != 0)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8", errors="backslashreplace")
    raise SystemExit(main())
