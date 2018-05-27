
public class test {

    public static void main(String[] args) {

        NetworkCommunication net = new NetworkClass(1, "192.168.178.50");

        if (!net.udpInitialize(42100)) {
            System.err.println("Inizializzazione fallita...");
            return;
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
