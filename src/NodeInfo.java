
public class NodeInfo {

    private final String ip;
    private final int id;
    private int not_seen;
    private int path_length;

    public NodeInfo(String ip, int id, int path_length) {
        this.ip = ip;
        this.id = id;
        this.not_seen = 0;
        this.path_length = path_length;
    }

    public String get_ip() {
        return ip;
    }

    public int get_id() {
        return id;
    }

    public int get_notSeen() {
        return not_seen;
    }

    public void incrementNotSeen() {
        not_seen++;
    }

    public void clearNotSeen() {
        not_seen = 0;
    }

    public void setPathLength(int path_length) {
        this.path_length = path_length;
    }

    public int getPathLength() {
        return path_length;
    }
}
