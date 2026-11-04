# Optional: deploy the EMQX hub with Sidekick (https://github.com/MightyMoud/sidekick):
#   cd HiveMQ/global/emqx && sidekick launch   (HTTP port 18083 = the dashboard behind Traefik + SSL)
# Sidekick routes HTTP only - open MQTT 1883 / WebSocket 8083 on the VPS too (see README.md).
FROM emqx/emqx:5.8
ENV EMQX_ALLOW_ANONYMOUS=false \
    EMQX_LISTENERS__TCP__DEFAULT__TCP_OPTIONS__NODELAY=true \
    EMQX_MQTT__MAX_MQUEUE_LEN=200 \
    EMQX_MQTT__MAX_PACKET_SIZE=256KB
EXPOSE 1883 8083 18083
