void parse_mqtt_msg(char type) {
  switch ((type & 0xF0)) {
    case MQTT_PACKET_CONNECT:
      Debug.printf("(CONNECT)");
      break;
    case MQTT_PACKET_CONNACK:
      Debug.printf("(CONNACK)");
      break;
    case MQTT_PACKET_PUBLISH:
      Debug.printf("(PUBLISH)");
      break;
    case MQTT_PACKET_PUBACK:
      Debug.printf("(PUBACK)");
      break;
    case MQTT_PACKET_PUBREC:
      Debug.printf("(PUBREC)");
      break;
    case MQTT_PACKET_PUBREL:
      Debug.printf("(PUBREL)");
      break;
    case MQTT_PACKET_PUBCOMP:
      Debug.printf("(PUBCOMP)");
      break;
    case MQTT_PACKET_SUBSCRIBE:
      Debug.printf("(SUBSCRIBE)");
      break;
    case MQTT_PACKET_SUBACK:
      Debug.printf("(SUBACK)");
      break;
    case MQTT_PACKET_UNSUBSCRIBE:
      Debug.printf("(USNUBSCRIBE)");
      break;
    case MQTT_PACKET_UNSUBACK:
      Debug.printf("(UNSUBACK)");
      break;
    case MQTT_PACKET_PINGREQ:
      Debug.printf("(PINGREQ)");
      break;
    case MQTT_PACKET_PINGRESP:
      Debug.printf("(PINGRESP)");
      break;
    case MQTT_PACKET_DISCONNECT:
      Debug.printf("(DISCONNECT)");
      break;
    default:
      Debug.printf("(UNKNOWN)");
      break;
  }
}
