
/**
 * Just a test class for receiving messages
 */
public class ThreadReceiver extends Thread {

    private NetworkCommunication net;
    byte[] buffer;

    ThreadReceiver(NetworkCommunication net) {
        this.net = net;
        this.buffer = new byte[512];
    }

    @Override
    public void run() {

        while (true) {
            int n = net.udpReceive(buffer);

            if (n > 0) {
                System.out.println("Received messages for application.");
                String mex = new String(buffer);
                mex = mex.substring(0, n);
                System.out.println("Message: " + mex);
            }

            if (n == 0) {
                System.out.println("Received a management message.");
            }

            if (n < 0) {
                System.err.println("Error in read! Stop the thread.");
                return;
            }

            for (int i=0; i<buffer.length; ++i) {
                buffer[i] = (byte) '\0';
            }
        }
    }
}
