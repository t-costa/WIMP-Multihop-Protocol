
class NodeInfo {

    private final String ip;
    private final String id;
    private int not_seen;
    private int path_length;

    NodeInfo(String ip, String id, int path_length) {
        this.ip = ip;
        this.id = id;
        this.not_seen = 0;
        this.path_length = path_length;
    }

    String getIp() {
        return ip;
    }

    String getId() {
        return id;
    }

    int getNotSeen() {
        return not_seen;
    }

    void incrementNotSeen() {
        not_seen++;
    }

    void clearNotSeen() {
        not_seen = 0;
    }

    void setPathLength(int path_length) {
        this.path_length = path_length;
    }

    int getPathLength() {
        return path_length;
    }
}
