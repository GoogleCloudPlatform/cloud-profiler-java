class Testo {
    public static void main(String[] args) throws InterruptedException {
        var t = new Thread(() -> {
            System.out.println("Hello!");
        });
        t.start();
        t.join();
    }
}
