import java.util.ArrayList;
import java.util.function.IntUnaryOperator;

public class Workloads {
    static final String[] texts = new String[16];
    static int arithmetic(int i) { return (1000 + i * 7 + 20 + i) - (i % 7) * (i % 11); }
    static int branch(int i) {
        int x = i * 4093 + 7;
        for (int j = 0; j < 128 + i; j++) {
            x += 251;
            if (x >= 65521) x -= 65521;
        }
        return x;
    }
    static int fibonacci(int i) {
        int a = 0, b = 1;
        for (int j = 0; j < 20 + i; j++) {
            int next = a + b; a = b; b = next;
        }
        return a;
    }
    static int list(int i) {
        ArrayList<Integer> values = new ArrayList<>();
        for (int j = 0; j < 128; j++) values.add((j * 17 + i) % 1000);
        int result = 0;
        for (int j = 0; j < 128; j++) {
            int value = values.get(j);
            if (value >= 500) result += value;
        }
        return result;
    }
    static int text(int i) {
        String result = texts[i].replace("Việt", "V++");
        return result.codePointCount(0, result.length());
    }
    public static void main(String[] args) {
        for (int i = 0; i < 16; i++) texts[i] = "Xin chào Việt Nam! ".repeat(i + 1);
        IntUnaryOperator work = switch (System.getenv("VPP_BENCH_CASE")) {
            case "arithmetic" -> Workloads::arithmetic;
            case "branch" -> Workloads::branch;
            case "fibonacci" -> Workloads::fibonacci;
            case "list" -> Workloads::list;
            case "text" -> Workloads::text;
            default -> throw new IllegalArgumentException("unknown workload");
        };
        int iterations = Integer.parseInt(System.getenv("VPP_BENCH_ITERS"));
        for (int i = 0; i < 16; i++) System.out.println(work.applyAsInt(i));
        double elapsed = 0, previous = -1, checksum = 0;
        for (int batch = 0; batch < 4; batch++) {
            checksum = 0;
            long start = System.nanoTime();
            for (int i = 0; i < iterations; i++) checksum += work.applyAsInt(i % 16);
            elapsed = (System.nanoTime() - start) / 1e6;
            if (previous >= 0 && previous != checksum) throw new AssertionError("unstable checksum");
            previous = checksum;
        }
        System.out.println(elapsed);
        System.out.println((long) checksum);
        System.out.println(iterations);
    }
}
