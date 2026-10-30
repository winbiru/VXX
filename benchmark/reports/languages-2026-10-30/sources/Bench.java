import java.util.ArrayList;
import java.util.function.IntUnaryOperator;

public final class Bench {
    static final String[] data = new String[16];
    static int arithmetic(int k) { return (k+17)*31+(k*7+3)-(k+5)*11; }
    static int branchLoop(int k) {
        int x=k*97+11;
        for(int i=0;i<256;i++) { x+=251; if(x>=65521) x-=65521; }
        return x;
    }
    static int fibonacci(int k) {
        int a=0,b=1;
        for(int i=0;i<k%16+20;i++) { int n=a+b; a=b; b=n; }
        return a;
    }
    static int list128(int k) {
        ArrayList<Integer> values=new ArrayList<>();
        for(int i=0;i<128;i++) values.add(k+i);
        int sum=0;
        for(int i=0;i<128;i++) if(values.get(i)%2==0) sum+=values.get(i);
        return sum;
    }
    static int unicodeReplace(int k) {
        String text=data[k%16].replace("Việt","V++");
        return text.codePointCount(0,text.length());
    }
    static double[] batch(IntUnaryOperator fn,int iterations,int seed,double expected) {
        double checksum=0;
        long start=System.nanoTime();
        for(int i=0;i<iterations;i++) checksum+=fn.applyAsInt(seed+i%16)%997;
        double elapsed=(System.nanoTime()-start)/1e6;
        if(checksum!=expected || elapsed<=0) throw new IllegalStateException("checksum/clock");
        return new double[]{checksum,elapsed};
    }
    public static void main(String[] args) {
        String name=System.getenv().getOrDefault("BENCH_CASE","arithmetic");
        int iterations=Integer.parseInt(System.getenv().getOrDefault("BENCH_ITERATIONS","16"));
        int seed=Integer.parseInt(System.getenv().getOrDefault("BENCH_SEED","17"));
        if(iterations<16 || iterations%16!=0 || iterations>100000000) throw new IllegalArgumentException("iterations");
        for(int i=0;i<16;i++) { int k=seed+i; data[k%16]="Xin chào Việt Nam 🌿 — Việt! ".repeat(k%4+4)+k; }
        IntUnaryOperator fn=switch(name) {
            case "arithmetic" -> Bench::arithmetic;
            case "branch_loop" -> Bench::branchLoop;
            case "fibonacci" -> Bench::fibonacci;
            case "list_128" -> Bench::list128;
            case "unicode_replace" -> Bench::unicodeReplace;
            default -> throw new IllegalArgumentException("unsupported case");
        };
        StringBuilder checks=new StringBuilder("[");
        double unit=0;
        for(int i=0;i<16;i++) { int value=fn.applyAsInt(seed+i); if(i>0) checks.append(','); checks.append(value); unit+=value%997; }
        checks.append(']');
        double expected=unit*(iterations/16);
        double[] warm=new double[3];
        for(int i=0;i<3;i++) warm[i]=batch(fn,iterations,seed,expected)[1];
        double[] measured=batch(fn,iterations,seed,expected);
        System.out.println("{\"case\":\""+name+"\",\"iterations\":"+iterations+",\"seed\":"+seed+
            ",\"checks\":"+checks+",\"checksum\":\""+Long.toString((long)measured[0])+"\",\"elapsed_ms\":"+measured[1]+
            ",\"ns_per_op\":"+(measured[1]*1e6/iterations)+",\"warmup_ms\":["+
            warm[0]+","+warm[1]+","+warm[2]+"]}");
    }
}
