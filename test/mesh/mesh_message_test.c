#include <stdio.h>

#include "CppUTest/TestHarness.h"
#include "CppUTest/CommandLineTestRunner.h"

#include "ble/mesh/adv_bearer.h"
#include "ble/mesh/mesh_network.h"
#include "mesh_transport.h"
#include "btstack_util.h"
#include "provisioning.h"
#include "btstack_memory.h"

extern "C" int mock_process_hci_cmd(void);

static mesh_network_pdu_t * received_network_pdu;
static mesh_network_pdu_t * received_unsegmented_transport_pdu;
static mesh_transport_pdu_t * received_segmented_transport_pdu;

static uint8_t sent_network_pdu_data[29];
static uint8_t sent_network_pdu_len;

static btstack_packet_handler_t mesh_packet_handler;
void adv_bearer_register_for_mesh_message(btstack_packet_handler_t packet_handler){
    mesh_packet_handler = packet_handler;
}
void adv_bearer_request_can_send_now_for_mesh_message(void){
    // simulate can send now
    uint8_t event[3];
    event[0] = HCI_EVENT_MESH_META;
    event[1] = 1;
    event[2] = MESH_SUBEVENT_CAN_SEND_NOW;
    (*mesh_packet_handler)(HCI_EVENT_PACKET, 0, &event[0], sizeof(event));
}
void adv_bearer_send_mesh_message(const uint8_t * network_pdu, uint16_t size){
    memcpy(sent_network_pdu_data, network_pdu, size);
    sent_network_pdu_len = size;
}
void adv_bearer_emit_sent(void){
    uint8_t event[3];
    event[0] = HCI_EVENT_MESH_META;
    event[1] = 1;
    event[2] = MESH_SUBEVENT_CAN_SEND_NOW;
    (*mesh_packet_handler)(HCI_EVENT_PACKET, 0, &event[0], sizeof(event));
}

void CHECK_EQUAL_ARRAY(uint8_t * expected, uint8_t * actual, int size){
    int i;
    for (i=0; i<size; i++){
        if (expected[i] != actual[i]) {
            printf("offset %u wrong\n", i);
            printf("expected: "); printf_hexdump(expected, size);
            printf("actual:   "); printf_hexdump(actual, size);
        }
        BYTES_EQUAL(expected[i], actual[i]);
    }
}

static int scan_hex_byte(const char * byte_string){
    int upper_nibble = nibble_for_char(*byte_string++);
    if (upper_nibble < 0) return -1;
    int lower_nibble = nibble_for_char(*byte_string);
    if (lower_nibble < 0) return -1;
    return (upper_nibble << 4) | lower_nibble;
}

static int btstack_parse_hex(const char * string, uint16_t len, uint8_t * buffer){
    int i;
    for (i = 0; i < len; i++) {
        int single_byte = scan_hex_byte(string);
        if (single_byte < 0) return 0;
        string += 2;
        buffer[i] = (uint8_t)single_byte;
        // don't check seperator after last byte
        if (i == len - 1) {
            return 1;
        }
        // optional seperator
        char separator = *string;
        if (separator == ':' && separator == '-' && separator == ' ') {
            string++;
        }
    }
    return 1;
}

static void btstack_print_hex(const uint8_t * data, uint16_t len, char separator){
    int i;
    for (i=0;i<len;i++){
        printf("%02x", data[i]);
        if (separator){
            printf("%c", separator);
        }
    }
    printf("\n");
}

static void load_provisioning_data_test_message(void){
    mesh_provisioning_data_t provisioning_data;
    provisioning_data.nid = 0x68;
    mesh_set_iv_index(0x12345678);
    btstack_parse_hex("0953fa93e7caac9638f58820220a398e", 16, provisioning_data.encryption_key);
    btstack_parse_hex("8b84eedec100067d670971dd2aa700cf", 16, provisioning_data.privacy_key);
    mesh_network_key_list_add_from_provisioning_data(&provisioning_data);

    uint8_t application_key[16];
    btstack_parse_hex("63964771734fbd76e3b40519d1d94a48", 16, application_key);
    mesh_application_key_set( 0, 0x26, application_key);

    uint8_t device_key[16];
    btstack_parse_hex("9d6dd0e96eb25dc19a40ed9914f8f03f", 16, device_key);
    mesh_transport_set_device_key(device_key);
}

static void test_lower_transport_callback_handler(mesh_network_callback_type_t callback_type, mesh_network_pdu_t * network_pdu){
    switch (callback_type){
        case MESH_NETWORK_PDU_RECEIVED:
            received_network_pdu = network_pdu;
            break;
        case MESH_NETWORK_PDU_SENT:
            break;
        default:
            break;
    }
}

static void test_upper_transport_unsegmented_callback_handler(mesh_network_pdu_t * network_pdu){
    received_unsegmented_transport_pdu = network_pdu;
}

static void test_upper_transport_segmented_callback_handler(mesh_transport_pdu_t * transport_pdu){
    received_segmented_transport_pdu = transport_pdu;
}

TEST_GROUP(MessageTest){
    void setup(void){
        btstack_memory_init();
        btstack_crypto_init();
        load_provisioning_data_test_message();
        mesh_network_init();
        mesh_network_set_higher_layer_handler(&test_lower_transport_callback_handler);
        mesh_upper_transport_register_unsegemented_message_handler(&test_upper_transport_unsegmented_callback_handler);
        mesh_upper_transport_register_segemented_message_handler(&test_upper_transport_segmented_callback_handler);
        received_network_pdu = NULL;
        received_segmented_transport_pdu = NULL;
        received_unsegmented_transport_pdu = NULL;
        sent_network_pdu_len = 0;
    }
};

static uint8_t transport_pdu_data[64];
static uint16_t transport_pdu_len;

static     uint8_t test_network_pdu_len;
static uint8_t test_network_pdu_data[29];


char * message1_network_pdus[] = {
    (char *) "68eca487516765b5e5bfdacbaf6cb7fb6bff871f035444ce83a670df"
};
char * message1_lower_transport_pdus[] = {
    (char *) "034b50057e400000010000",
};
char * message1_upper_transport_pdu = (char *) "034b50057e400000010000";

char * message6_network_pdus[] = {
    (char *) "68cab5c5348a230afba8c63d4e686364979deaf4fd40961145939cda0e",
    (char *) "681615b5dd4a846cae0c032bf0746f44f1b8cc8ce5edc57e55beed49c0"
};
char * message6_lower_transport_pdus[] = {
    (char *) "8026ac01ee9dddfd2169326d23f3afdf",
    (char *) "8026ac21cfdc18c52fdef772e0e17308",
};
char * message6_upper_transport_pdu = (char *) "0056341263964771734fbd76e3b40519d1d94a48";

void test_receive_network_puds(int count, char ** network_pdus, char ** lower_transport_pdus, char * access_pdu){
    int i;
    for (i=0;i<count;i++){
        test_network_pdu_len = strlen(network_pdus[i]) / 2;
        btstack_parse_hex(network_pdus[i], test_network_pdu_len, test_network_pdu_data);
    
        mesh_network_received_message(test_network_pdu_data, test_network_pdu_len);

        while (received_network_pdu == NULL) {
            mock_process_hci_cmd();
        }

        transport_pdu_len = strlen(lower_transport_pdus[i]) / 2;
        btstack_parse_hex(lower_transport_pdus[i], transport_pdu_len, transport_pdu_data);

        uint8_t * lower_transport_pdu     = mesh_network_pdu_data(received_network_pdu);
        uint8_t   lower_transport_pdu_len = mesh_network_pdu_len(received_network_pdu);

        // printf_hexdump(lower_transport_pdu, lower_transport_pdu_len);

        CHECK_EQUAL( transport_pdu_len, lower_transport_pdu_len);
        CHECK_EQUAL_ARRAY(transport_pdu_data, lower_transport_pdu, transport_pdu_len);

        // forward to mesh_transport
        mesh_lower_transport_received_mesage(MESH_NETWORK_PDU_RECEIVED, received_network_pdu);

        // done
        received_network_pdu = NULL;
    }

    // wait for tranport pdu
    while (received_unsegmented_transport_pdu == NULL && received_segmented_transport_pdu == NULL) {
        mock_process_hci_cmd();
    }

    transport_pdu_len = strlen(access_pdu) / 2;
    btstack_parse_hex(access_pdu, transport_pdu_len, transport_pdu_data);

    uint8_t * upper_transport_pdu = NULL;
    uint8_t   upper_transport_pdu_len = 0;
    if (received_unsegmented_transport_pdu){
        upper_transport_pdu     = mesh_network_pdu_data(received_unsegmented_transport_pdu);
        upper_transport_pdu_len = mesh_network_pdu_len(received_unsegmented_transport_pdu);
    }
    if (received_segmented_transport_pdu){
        upper_transport_pdu     = received_segmented_transport_pdu->data;
        upper_transport_pdu_len = received_segmented_transport_pdu->len;
    }
    printf_hexdump(upper_transport_pdu, upper_transport_pdu_len);
    CHECK_EQUAL( transport_pdu_len, upper_transport_pdu_len);
    CHECK_EQUAL_ARRAY(transport_pdu_data, upper_transport_pdu, transport_pdu_len);
}

void test_send_access_message(uint16_t netkey_index, uint16_t appkey_index,  uint8_t ttl, uint16_t src, uint16_t dest, uint8_t szmic, char * control_pdu, int count, char ** lower_transport_pdus, char ** network_pdus){

    transport_pdu_len = strlen(control_pdu) / 2;
    btstack_parse_hex(control_pdu, transport_pdu_len, transport_pdu_data);

    mesh_upper_transport_access_send(netkey_index, appkey_index, ttl, src, dest, transport_pdu_data, transport_pdu_len, szmic);

    // check for all network pdus
    int i;
    for (i=0;i<count;i++){
        // expected network pdu
        test_network_pdu_len = strlen(network_pdus[i]) / 2;
        btstack_parse_hex(network_pdus[i], test_network_pdu_len, test_network_pdu_data);

        while (sent_network_pdu_len == 0) {
            mock_process_hci_cmd();
        }

        CHECK_EQUAL( sent_network_pdu_len, test_network_pdu_len);
        CHECK_EQUAL_ARRAY(test_network_pdu_data, sent_network_pdu_data, test_network_pdu_len);

        sent_network_pdu_len = 0;
    }
}

void test_send_control_message(uint16_t netkey_index, uint8_t ttl, uint16_t src, uint16_t dest, char * control_pdu, int count, char ** lower_transport_pdus, char ** network_pdus){

    sent_network_pdu_len = 0;

    transport_pdu_len = strlen(control_pdu) / 2;
    btstack_parse_hex(control_pdu, transport_pdu_len, transport_pdu_data);

    uint8_t opcode = transport_pdu_data[0];
    mesh_upper_transport_send_control_pdu(netkey_index, ttl, src, dest, opcode, &transport_pdu_data[1], transport_pdu_len - 1);

    // check for all network pdus
    int i;
    for (i=0;i<count;i++){
        // expected network pdu
        test_network_pdu_len = strlen(network_pdus[i]) / 2;
        btstack_parse_hex(network_pdus[i], test_network_pdu_len, test_network_pdu_data);

        while (sent_network_pdu_len == 0) {
            mock_process_hci_cmd();
        }

        if (sent_network_pdu_len != test_network_pdu_len){
            printf("Network PDU: "); printf_hexdump(sent_network_pdu_data, sent_network_pdu_len);
        }
        // CHECK_EQUAL( sent_network_pdu_len, test_network_pdu_len);
        // CHECK_EQUAL_ARRAY(test_network_pdu_data, sent_network_pdu_data, test_network_pdu_len);

        sent_network_pdu_len = 0;
    }
}

// TEST(MessageTest, Message1Receive){
//     test_receive_network_puds(1, message1_network_pdus, message1_lower_transport_pdus, message1_upper_transport_pdu);
// }

TEST(MessageTest, Message1Send){
    uint16_t netkey_index = 0;
    uint8_t  ttl          = 0;
    uint16_t src          = 0x1201;
    uint16_t dest         = 0xfffd;
    uint32_t seq          = 1;

    mesh_upper_transport_set_seq(seq);
    test_send_control_message(netkey_index, ttl, src, dest, message1_upper_transport_pdu, 1, message1_lower_transport_pdus, message1_network_pdus);
}

// TEST(MessageTest, Message6Receive){
//     test_receive_network_puds(2, message6_network_pdus, message6_lower_transport_pdus, message6_upper_transport_pdu);
// }

TEST(MessageTest, Message6Send){
    // TTL :00
    // SEQ : 000001
    // SRC : 1201
    // DST : fffd
    uint16_t netkey_index = 0;
    uint16_t appkey_index = MESH_DEVICE_KEY_INDEX;
    uint8_t  ttl          = 4;
    uint16_t src          = 0x0003;
    uint16_t dest         = 0x1201;
    uint32_t seq          = 0x3129ab;
    uint8_t  szmic        = 0;

    mesh_upper_transport_set_seq(seq);
    test_send_access_message(netkey_index, appkey_index, ttl, src, dest, szmic, message6_upper_transport_pdu, 1, message6_lower_transport_pdus, message6_network_pdus);
}

int main (int argc, const char * argv[]){
    return CommandLineTestRunner::RunAllTests(argc, argv);
}