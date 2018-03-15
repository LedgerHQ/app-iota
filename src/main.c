#include "main.h"
#include "ui.h"
#include "apdu.h"
#include "aux.h"

// iota-related stuff
#include "iota/kerl.h"
#include "iota/conversion.h"
#include "iota/addresses.h"
#include "iota/bundle.h"
#include "iota/signing.h"

// use internalStorage_t to temp hold the storage
typedef struct internalStorage_t {
    uint8_t initialized;
    uint32_t account_seed[5];
    uint8_t advanced_mode;
    uint8_t browser_mode;

} internalStorage_t;

// N_storage_real will hold the actual address for NVRAM
WIDE internalStorage_t N_storage_real;
#define N_storage (*(WIDE internalStorage_t *)PIC(&N_storage_real))

unsigned char G_io_seproxyhal_spi_buffer[IO_SEPROXYHAL_BUFFER_SIZE_B];
ux_state_t ux;

unsigned int state_flags;

unsigned char seed_bytes[48];

int64_t balance;
int64_t payment;

BUNDLE_CTX bundle_ctx;
SIGNING_CTX signing_ctx;

// This symbol is defined by the link script to be at the start of the stack
extern unsigned long _stack;

#define STACK_CANARY (*((volatile uint32_t *)&_stack))

void init_canary()
{
    STACK_CANARY = 0xDEADBEEF;
}

void check_canary()
{
    if (STACK_CANARY != 0xDEADBEEF) {
        THROW(0x0077);
        // THROW(EXCEPTION_OVERFLOW);
    }
}

uint8_t get_advanced_mode()
{
    return N_storage.advanced_mode;
}

uint8_t get_browser_mode()
{
    return N_storage.browser_mode;
}

void write_advanced_mode(uint8_t mode)
{
    // something must have gone wrong to receive a mode > 1
    if (mode > 1) {
        os_sched_exit(0);
        return;
    }

    // only write if mode is different
    if (mode != N_storage.advanced_mode)
        nvm_write(&N_storage.advanced_mode, (void *)&mode, sizeof(uint8_t));
}

void write_browser_mode(uint8_t mode)
{
    // something must have gone wrong to receive a mode > 1
    if (mode > 1) {
        os_sched_exit(0);
        return;
    }

    // only write if mode is different
    if (mode != N_storage.browser_mode)
        nvm_write(&N_storage.browser_mode, (void *)&mode, sizeof(uint8_t));
}

bool flash_is_init()
{
    return N_storage.initialized == 0x01;
}

void init_flash()
{
    internalStorage_t storage;

    storage.initialized = 0x01;
    memset(storage.account_seed, 0, sizeof(uint32_t) * 5);
    storage.account_seed[0] = 1;
    storage.account_seed[1] = 4;
    storage.account_seed[2] = 9;
    storage.account_seed[3] = 22;
    storage.account_seed[4] = 762;
    storage.advanced_mode = 0;
    storage.browser_mode = 0;

    nvm_write(&N_storage, (void *)&storage, sizeof(internalStorage_t));
}

uint32_t get_seed_idx(unsigned int idx)
{
    return N_storage.account_seed[idx];
}

void apdu_return(unsigned int tx)
{
    G_io_apdu_buffer[tx++] = 0x90;
    G_io_apdu_buffer[tx++] = 0x00;

    // Send back the response, do not restart the event loop
    io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, tx);
}

void user_deny()
{
    balance = 0;
    payment = 0;

    apdu_return(0);
}

void user_sign()
{
    ui_display_calc();
    ui_force_draw();

    TX_OUTPUT *output = (TX_OUTPUT *)(G_io_apdu_buffer);
    os_memset(output, 0, sizeof(TX_OUTPUT));

    output->tag_increment = bundle_finalize(&bundle_ctx);

    output->finalized = true;
    state_flags |= BUNDLE_FINALIZED;

    bytes_to_chars(bundle_get_hash(&bundle_ctx), output->bundle_hash, 48);

    balance = 0;
    payment = 0;
    apdu_return(sizeof(TX_OUTPUT));
}

void __attribute__((noinline))
ins_set_seed(unsigned char *msg, const uint8_t len)
{
    if (!flash_is_init())
        THROW(APP_UNINITIALIZED);

    if (CHECK_STATE(state_flags, SET_SEED)) {
        THROW(INVALID_STATE);
    }
    if (len < sizeof(SET_SEED_INPUT)) {
        THROW(0x6D09);
    }

    SET_SEED_INPUT *input = (SET_SEED_INPUT *)(msg);

    unsigned int path[BIP44_PATH_LEN];
    for (unsigned int i = 0; i < BIP44_PATH_LEN; i++) {
        if (!ASSIGN(path[i], input->bip44_path[i]))
            THROW(INVALID_PARAMETER);
    }

    // we only care about privateKeyData and using this to
    // generate our iota seed
    unsigned char entropy[64];
    os_perso_derive_node_bip32(CX_CURVE_256K1, path, BIP44_PATH_LEN, entropy,
                               entropy + 32);
    get_seed(entropy, sizeof(entropy), seed_bytes);
    // getting the seed resets everything
    state_flags = SEED_SET;

    // return success
    THROW(0x9000);
}

void __attribute__((noinline)) ins_pubkey(unsigned char *msg, const uint8_t len)
{
    if (!flash_is_init())
        THROW(APP_UNINITIALIZED);

    if (CHECK_STATE(state_flags, PUBKEY)) {
        THROW(INVALID_STATE);
    }
    if (len < sizeof(PUBKEY_INPUT)) {
        THROW(0x6D09);
    }

    // temp display screen
    ui_display_calc();
    ui_force_draw();

    unsigned char addr_bytes[48];

    PUBKEY_INPUT *input = (PUBKEY_INPUT *)(msg);

    get_public_addr(seed_bytes, input->address_idx, SECURITY_LEVEL, addr_bytes);

    PUBKEY_OUTPUT *output = (PUBKEY_OUTPUT *)(G_io_apdu_buffer);
    os_memset(output, 0, sizeof(PUBKEY_OUTPUT));

    // convert the 48 byte address into base-27 address
    bytes_to_chars(addr_bytes, output->address, 48);

    ui_build_display();
    ui_render();
    ui_force_draw();

    // return success
    apdu_return(sizeof(PUBKEY_OUTPUT));
}

void __attribute__((noinline))
ins_tx(unsigned char *msg, const uint8_t len, volatile unsigned int *flags,
       volatile int64_t *balance, volatile int64_t *payment)
{
    if (!flash_is_init())
        THROW(APP_UNINITIALIZED);

    if (CHECK_STATE(state_flags, TX)) {
        THROW(INVALID_STATE);
    }
    if (len < sizeof(TX_INPUT)) {
        THROW(0x6D09);
    }

    // temporary screen while receiving tx
    ui_display_recv();
    ui_force_draw();

    TX_INPUT *input = (TX_INPUT *)(msg);

    if ((state_flags & BUNDLE_INITIALIZED) == 0) {
        uint32_t last_index;
        if (!ASSIGN(last_index, input->last_index)) {
            THROW(INVALID_PARAMETER); // overflow
        }
        bundle_initialize(&bundle_ctx, last_index);
        state_flags |= BUNDLE_INITIALIZED;
    }

    // validate transaction indices
    if (input->last_index != bundle_ctx.last_index) {
        THROW(INVALID_STATE);
    }
    if (input->current_index != bundle_ctx.current_index) {
        THROW(INVALID_STATE);
    }

    if (!validate_chars(input->address, 81, false)) {
        THROW(INVALID_PARAMETER);
    }
    bundle_set_address_chars(&bundle_ctx, input->address);

    if (!validate_chars(input->tag, 27, true)) {
        THROW(INVALID_PARAMETER);
    }
    uint32_t timestamp;
    if (!ASSIGN(timestamp, input->timestamp)) {
        THROW(INVALID_PARAMETER); // overflow
    }
    bundle_add_tx(&bundle_ctx, input->value, input->tag, timestamp);

    if (input->value >= 0)
        *payment += input->value;
    else // create meta tx for input
    {
        *balance -= input->value;

        bundle_set_address_chars(&bundle_ctx, input->address);

        bundle_add_tx(&bundle_ctx, 0, input->tag, timestamp);
    }

    // TODO: add change address
    if (bundle_ctx.current_index > bundle_ctx.last_index) {
        const unsigned char *addr_bytes =
            bundle_get_address_bytes(&bundle_ctx, 0);
        char address[81];

        bytes_to_chars(addr_bytes, address, 48);
        // display
        *flags |= IO_ASYNCH_REPLY;

        ui_sign_tx(*balance, *payment, address, 81);
    }
    else // return success
    {
        TX_OUTPUT *output = (TX_OUTPUT *)(G_io_apdu_buffer);
        os_memset(output, 0, sizeof(TX_OUTPUT));

        apdu_return(sizeof(TX_OUTPUT));
    }
}

void __attribute__((noinline))
ins_sign(unsigned char *msg, const uint8_t len, volatile unsigned int *flags)
{
    if (!flash_is_init())
        THROW(APP_UNINITIALIZED);

    if (CHECK_STATE(state_flags, SIGN)) {
        THROW(INVALID_STATE);
    }
    if (len < sizeof(SIGN_INPUT)) {
        THROW(0x6D09);
    }

    // temporary screen during signing process
    ui_display_sending();
    ui_force_draw();

    SIGN_INPUT *input = (SIGN_INPUT *)(msg);

    if ((state_flags & SIGNING_STARTED) == 0) {
        tryte_t normalized_hash[81];
        normalize_hash_bytes(bundle_get_hash(&bundle_ctx), normalized_hash);

        // TODO: the transaction index is not the address index
        signing_initialize(&signing_ctx, seed_bytes, input->transaction_idx,
                           SECURITY_LEVEL, normalized_hash);

        state_flags |= SIGNING_STARTED;
    }

    // TODO: check that the transaction idx has not changed

    SIGN_OUTPUT *output = (SIGN_OUTPUT *)(G_io_apdu_buffer);
    os_memset(output, 0, sizeof(SIGN_OUTPUT));

    {
        unsigned char fragment_bytes[SIGNATURE_FRAGMENT_SIZE * 48];
        output->fragment_index =
            signing_next_fragment(&signing_ctx, fragment_bytes);

        bytes_to_chars(fragment_bytes, output->signature_fragment,
                       SIGNATURE_FRAGMENT_SIZE * 48);
    }
    output->last_fragment = NUM_SIGNATURE_FRAGMENTS(SECURITY_LEVEL) - 1;


    if (output->fragment_index == output->last_fragment) {
        state_flags &= ~SIGNING_STARTED;

        *flags |= IO_ASYNCH_REPLY;
        apdu_return(sizeof(SIGN_OUTPUT));
        ui_display_welcome();
    }

    // return success
    apdu_return(sizeof(SIGN_OUTPUT));
}

void __attribute__((noinline))
ins_display_address(unsigned char *msg, const uint8_t len)
{
    if (!flash_is_init())
        THROW(APP_UNINITIALIZED);

    if (len < sizeof(PUBKEY_INPUT)) {
        THROW(0x6D09);
    }

    ui_display_calc();
    ui_force_draw();

    unsigned char addr_bytes[48];

    PUBKEY_INPUT *input = (PUBKEY_INPUT *)(msg);

    get_public_addr(seed_bytes, input->address_idx, SECURITY_LEVEL, addr_bytes);

    char address[81];
    // convert the 48 byte address into base-27 address
    bytes_to_chars(addr_bytes, address, 48);

    // ui_display_address(address, 81);
    // ui_force_draw();

    // return success
    THROW(0x9000);
}

static void IOTA_main()
{
    volatile unsigned int rx = 0;
    volatile unsigned int tx = 0;
    volatile unsigned int flags = 0;

    balance = 0;
    payment = 0;

    // initialize the UI
    ui_init(flash_is_init());

    // DESIGN NOTE: the bootloader ignores the way APDU are fetched. The only
    // goal is to retrieve APDU.
    // When APDU are to be fetched from multiple IOs, like NFC+USB+BLE, make
    // sure the io_event is called with a
    // switch event, before the apdu is replied to the bootloader. This avoid
    // APDU injection faults.
    for (;;) {
        volatile unsigned short sw = 0;

        BEGIN_TRY
        {
            TRY
            {
                rx = tx;
                // ensure no race in catch_other if io_exchange throws an error
                tx = 0;

                check_canary();
                rx = io_exchange(CHANNEL_APDU | flags, rx);

                // flags sets the IO to blocking, make sure to re-enable asynch
                flags = 0;

                // no apdu received, well, reset the session, and reset the
                // bootloader configuration
                if (rx == 0)
                    THROW(0x6982);

                // If it doesnt start with the magic byte return error
                // CLA is 0x80
                if (G_io_apdu_buffer[0] != CLA)
                    THROW(0x6E00);

                const uint8_t len = G_io_apdu_buffer[APDU_BODY_LENGTH_OFFSET];
                if (rx < len + APDU_HEADER_LENGTH) {
                    THROW(INVALID_STATE);
                }
                unsigned char *msg = G_io_apdu_buffer + APDU_HEADER_LENGTH;

                // check second byte for instruction
                switch (G_io_apdu_buffer[1]) {

                case INS_SET_SEED: {
                    ins_set_seed(msg, len);
                } break;

                case INS_PUBKEY: {
                    ins_pubkey(msg, len);
                } break;

                case INS_TX: {
                    ins_tx(msg, len, &flags, &balance, &payment);
                } break;

                case INS_SIGN: {
                    ins_sign(msg, len, &flags);
                } break;

                case INS_DISP_ADDR: {
                    ins_display_address(msg, len);
                } break;

                case 0xFF: // return to dashboard
                    goto return_to_dashboard;

                    // unknown command ??
                default:
                    THROW(0x6D00);
                    break;
                }
            }
            CATCH_OTHER(e)
            {
                // ui_reset();

                switch (e & 0xF000) {
                case 0x6000:
                case 0x9000:
                    sw = e;
                    break;
                default:
                    sw = 0x6800 | (e & 0x7FF);
                    break;
                }
                // Unexpected exception => report
                G_io_apdu_buffer[tx] = sw >> 8;
                G_io_apdu_buffer[tx + 1] = sw;
                tx += 2;
            }
            FINALLY
            {
            }
        }
        END_TRY;
    }

return_to_dashboard:
    return;
}


/* ------------------------------------------------
 ---------------------------------------------------
 ---------------------------------------------------
 ------------------- Not Modified ------------------
 --------------------------------------------------- */


// seems to be called from io_exchange
unsigned short io_exchange_al(unsigned char channel, unsigned short tx_len)
{
    switch (channel & ~(IO_FLAGS)) {
    case CHANNEL_KEYBOARD:
        break;

        // multiplexed io exchange over a SPI channel and TLV encapsulated
        // protocol
    case CHANNEL_SPI:
        if (tx_len) {
            io_seproxyhal_spi_send(G_io_apdu_buffer, tx_len);

            if (channel & IO_RESET_AFTER_REPLIED) {
                reset();
            }
            return 0; // nothing received from the master so far (it's a tx
            // transaction)
        }
        else {
            return io_seproxyhal_spi_recv(G_io_apdu_buffer,
                                          sizeof(G_io_apdu_buffer), 0);
        }

    default:
        THROW(INVALID_PARAMETER);
    }
    return 0;
}

void io_seproxyhal_display(const bagl_element_t *element)
{
    io_seproxyhal_display_default((bagl_element_t *)element);
}

unsigned char io_event(unsigned char channel)
{
    // nothing done with the event, throw an error on the transport layer if
    // needed

    // can't have more than one tag in the reply, not supported yet.
    switch (G_io_seproxyhal_spi_buffer[0]) {
    case SEPROXYHAL_TAG_FINGER_EVENT:
        UX_FINGER_EVENT(G_io_seproxyhal_spi_buffer);
        break;

    case SEPROXYHAL_TAG_BUTTON_PUSH_EVENT: // for Nano S
        UX_BUTTON_PUSH_EVENT(G_io_seproxyhal_spi_buffer);
        break;

    case SEPROXYHAL_TAG_DISPLAY_PROCESSED_EVENT:
        if (UX_DISPLAYED()) {
            // TODO perform actions after all screen elements have been
            // displayed
        }
        else {
            UX_DISPLAYED_EVENT();
        }
        break;

        // unknown events are acknowledged
    default:
        break;
    }

    // close the event if not done previously (by a display or whatever)
    if (!io_seproxyhal_spi_is_status_sent()) {
        io_seproxyhal_general_status();
    }

    // command has been processed, DO NOT reset the current APDU transport
    return 1;
}

__attribute__((section(".boot"))) int main(void)
{
    // exit critical section
    __asm volatile("cpsie i");

    UX_INIT();

    // ensure exception will work as planned
    os_boot();

    init_canary();

    BEGIN_TRY
    {
        TRY
        {
            io_seproxyhal_init();

#ifdef LISTEN_BLE
            if (os_seph_features() &
                SEPROXYHAL_TAG_SESSION_START_EVENT_FEATURE_BLE) {
                BLE_power(0, NULL);
                // restart IOs
                BLE_power(1, NULL);
            }
#endif

            USB_power(0);
            USB_power(1);

            IOTA_main();
        }
        CATCH_OTHER(e)
        {
        }
        FINALLY
        {
        }
    }
    END_TRY;
}
