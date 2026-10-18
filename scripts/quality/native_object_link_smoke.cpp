// Compile with the host C++ toolchain and link directly to V++ generated
// machine-code objects. Tests ordinary platform linkage and actual execution.
extern "C" int tra42();
extern "C" int tinh_hang();
extern "C" int tinh_am();
extern "C" int tinh_sau();
extern "C" int cong(int, int);
extern "C" int cong_dao(int, int);
extern "C" int tru(int, int);

int main() {
    return tra42() == 42 && tinh_hang() == 48 &&
                   tinh_am() == -7 && tinh_sau() == 86 &&
                   cong(12, 30) == 42 && cong_dao(12, 30) == 42 &&
                   cong_dao(-120, 12) == -108 &&
                   cong(-120, 12) == -108 && cong(-1, 1) == 0 &&
                   tru(30, 12) == 18 && tru(12, 30) == -18 &&
                   tru(-100, -20) == -80
        ? 0 : 1;
}
