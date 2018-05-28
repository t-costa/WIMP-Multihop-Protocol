
public class test {

    public static void main(String[] args) {

        NetworkCommunication net = new NetworkClass(1, "10.13.3.145", 100);

        while (!net.udpInitialize(42100)) {
            System.err.println("Inizializzazione fallita... Trying again in 10 seconds");
            try {
                Thread.sleep(10000);
            } catch (InterruptedException e) {
                e.printStackTrace();
                return;
            }
        }


        ThreadSender sender = new ThreadSender(net);
        ThreadReceiver receiver = new ThreadReceiver(net);

        receiver.start();
        sender.start();

        try {
            receiver.join();
            sender.join();
        } catch (InterruptedException e) {
            e.printStackTrace();
        }

    }

}
