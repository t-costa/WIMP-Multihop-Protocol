
/**
 * Just a test class for sending messages
 */
import org.json.JSONObject;

public class ThreadSender extends Thread{

    private NetworkCommunication net;

    ThreadSender(NetworkCommunication net) {
        this.net = net;
    }

    @Override
    public void run() {
        int i = 0;
        while (true) {
           JSONObject jmex = new JSONObject();
           jmex.put("gianni", "pesca");
           jmex.put("id_incrementale", i);
           String mex = jmex.toString();

           if (!net.udpSend(mex)) {
               System.err.println("Send fallita...");
               return;
           } else {
               System.out.println("Send conclusa con successo!");
           }

            try {
                Thread.sleep(10000);
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
            i++;
        }
    }
}
