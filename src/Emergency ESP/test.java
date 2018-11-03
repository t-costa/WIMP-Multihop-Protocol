
/**
 * Just a basic test
 */
public class test {

    public static void main(String[] args) {

        NetworkCommunication net = new NetworkClass(1, "192.168.178.50", "D100");

        while (!net.udpInitialize(42100)) {
            System.err.println("Initialization failed... Trying again in 10 seconds");
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
