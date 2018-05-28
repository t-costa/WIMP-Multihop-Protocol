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
                System.out.println("Ricevuto veri e propri dati per l'applicazione");
                String mex = new String(buffer);
                mex = mex.substring(0, n);
                System.out.println("Messaggio: " + mex);
            }

            if (n == 0) {
                System.out.println("Ricevuto un messaggio di gestione");
            }

            if (n < 0) {
                System.err.println("C'è stato un errore nella read! chiudo tutto");
                return;
            }

            for (int i=0; i<buffer.length; ++i) {
                buffer[i] = 0;
            }
        }
    }
}
