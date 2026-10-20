#include "services/purchases.h"

#include "services/methods.h"
#include "app/log.h"
#include "app/options.h"
#include "compat/jni/runtime.h"
#include "compat/jni/strings.h"
#include "runtime/callbacks.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { PURCHASE_PRODUCT_CAPACITY = 128, PURCHASE_PRODUCT_ID_CAPACITY = 256 };
typedef enum {
    PURCHASE_RESPONSE_SUCCESS = 0,
    PURCHASE_RESPONSE_UNAVAILABLE = 2
} PurchaseResponseStatus;
typedef struct {
    char id[PURCHASE_PRODUCT_ID_CAPACITY];
    int is_owned;
    int is_consumable;
    int delivery_finished;
} PurchaseProduct;
typedef struct {
    PurchaseProduct products[PURCHASE_PRODUCT_CAPACITY];
    unsigned product_count;
    char failed_product_id[PURCHASE_PRODUCT_ID_CAPACITY];
    PurchaseResponseStatus response_status;
    int registration_pending;
    int restore_pending;
    int delivery_pending;
    int failure_pending;
} PurchaseState;
static PurchaseState purchase_state;

static PurchaseProduct *find_product(const char *id) {
    for (unsigned i = 0; i < purchase_state.product_count; i++)
        if (!strcmp(purchase_state.products[i].id, id))
            return &purchase_state.products[i];
    return NULL;
}
static jobjectArray new_string_array(unsigned count) {
    jclass string_class = port_jni->FindClass(&port_jni, "java/lang/String");
    jobjectArray array = port_jni->NewObjectArray(&port_jni, count, string_class, NULL);
    port_jni->DeleteLocalRef(&port_jni, string_class);
    return array;
}
static void set_array_string(jobjectArray array, unsigned index, const char *value) {
    jstring value_string = port_jni->NewStringUTF(&port_jni, value);
    port_jni->SetObjectArrayElement(&port_jni, array, index, value_string);
    port_jni->DeleteLocalRef(&port_jni, value_string);
}

static jobjectArray catalog_product_ids(void) {
    jobjectArray ids = new_string_array(purchase_state.product_count);
    for (unsigned index = 0; index < purchase_state.product_count; index++)
        set_array_string(ids, index, purchase_state.products[index].id);
    return ids;
}
static void apply_consumable_ids(jobjectArray identifiers) {
    jsize count = port_jni->GetArrayLength(&port_jni, identifiers);
    for (jsize index = 0; index < count; index++) {
        jstring identifier = port_jni->GetObjectArrayElement(&port_jni, identifiers, index);
        char product_id[PURCHASE_PRODUCT_ID_CAPACITY];
        port_jni_string_copy_utf8(identifier, product_id, sizeof(product_id));
        PurchaseProduct *product = find_product(product_id);
        if (product)
            product->is_consumable = 1;
        port_jni->DeleteLocalRef(&port_jni, identifier);
    }
}
static void notify_restored_products(void) {
    unsigned owned_count = 0;
    for (unsigned i = 0; i < purchase_state.product_count; i++)
        if (purchase_state.products[i].is_owned)
            owned_count++;
    jobjectArray product_ids = new_string_array(owned_count),
                 receipt_data = new_string_array(owned_count),
                 receipt_signatures = new_string_array(owned_count);
    unsigned output_index = 0;
    for (unsigned i = 0; i < purchase_state.product_count; i++)
        if (purchase_state.products[i].is_owned) {
            set_array_string(product_ids, output_index, purchase_state.products[i].id);
            set_array_string(receipt_data, output_index, "");
            set_array_string(receipt_signatures, output_index, "");
            output_index++;
        }
    port_game_purchases_restored(port_app_options.unlock_all ? JNI_TRUE : JNI_FALSE, product_ids,
                                 receipt_data, receipt_signatures);
    port_jni->DeleteLocalRef(&port_jni, product_ids);
    port_jni->DeleteLocalRef(&port_jni, receipt_data);
    port_jni->DeleteLocalRef(&port_jni, receipt_signatures);
}
static void complete_restore(void) {
    purchase_state.restore_pending = 0;
    notify_restored_products();
    if (port_app_options.unlock_all)
        purchase_state.delivery_pending = 1;
}

static void complete_registration(void) {
    purchase_state.registration_pending = 0;
    jobjectArray product_ids = catalog_product_ids();
    jobjectArray consumable_ids = port_game_purchase_consumables(product_ids);
    if (consumable_ids) {
        apply_consumable_ids(consumable_ids);
        port_jni->DeleteLocalRef(&port_jni, consumable_ids);
    }
    unsigned owned = 0, consumable = 0;
    for (unsigned i = 0; i < purchase_state.product_count; i++) {
        PurchaseProduct *purchase_product = &purchase_state.products[i];
        purchase_product->is_owned =
            port_app_options.unlock_all && !purchase_product->is_consumable;
        owned += purchase_product->is_owned != 0;
        consumable += purchase_product->is_consumable != 0;
    }
    port_log_debug("Purchases registered=%u owned=%u consumable=%u",
                   purchase_state.product_count, owned, consumable);
    jobjectArray failed_product_ids = new_string_array(0);
    port_game_purchase_registered(port_app_options.unlock_all ? JNI_TRUE : JNI_FALSE,
                                   failed_product_ids);
    port_jni->DeleteLocalRef(&port_jni, failed_product_ids);
    port_jni->DeleteLocalRef(&port_jni, product_ids);
    if (port_app_options.unlock_all)
        purchase_state.restore_pending = 1;
}

static void deliver_owned_products(void) {
    purchase_state.delivery_pending = 0;
    for (unsigned i = 0; i < purchase_state.product_count; i++)
        if (purchase_state.products[i].is_owned &&
            !purchase_state.products[i].delivery_finished) {
            purchase_state.products[i].delivery_finished = 1;
            jstring id = port_jni->NewStringUTF(&port_jni, purchase_state.products[i].id),
                    empty = port_jni->NewStringUTF(&port_jni, "");
            port_game_purchase_response(PURCHASE_RESPONSE_SUCCESS, id, empty, empty);
            port_jni->DeleteLocalRef(&port_jni, id);
            port_jni->DeleteLocalRef(&port_jni, empty);
        }
}

static void report_purchase_failure(void) {
    purchase_state.failure_pending = 0;
    jstring id = port_jni->NewStringUTF(&port_jni, purchase_state.failed_product_id),
            empty = port_jni->NewStringUTF(&port_jni, "");
    port_game_purchase_response(purchase_state.response_status, id, empty, empty);
    port_jni->DeleteLocalRef(&port_jni, id);
    port_jni->DeleteLocalRef(&port_jni, empty);
}
void port_purchases_poll(void) {
    if (purchase_state.registration_pending)
        complete_registration();
    if (purchase_state.restore_pending)
        complete_restore();
    if (purchase_state.delivery_pending)
        deliver_owned_products();
    if (purchase_state.failure_pending)
        report_purchase_failure();
}

static jvalue finish_purchase(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    char id[PURCHASE_PRODUCT_ID_CAPACITY];
    port_jni_string_copy_utf8((jstring)arguments[0].l, id, sizeof(id));
    PurchaseProduct *purchase_product = find_product(id);
    if (purchase_product)
        purchase_product->delivery_finished = 1;
    return (jvalue){0};
}

static jvalue purchase_receipt(const PortJniCall *call) {
    (void)call;
    jobjectArray receipt_fields = new_string_array(2);
    set_array_string(receipt_fields, 0, "");
    set_array_string(receipt_fields, 1, "");
    return (jvalue){.l = receipt_fields};
}

static jboolean query_product_owned(jstring identifier) {
    char product_id[PURCHASE_PRODUCT_ID_CAPACITY];
    port_jni_string_copy_utf8(identifier, product_id, sizeof(product_id));
    PurchaseProduct *product = find_product(product_id);
    return product && product->is_owned;
}
static jvalue is_item_owned(const PortJniCall *call) {
    return (jvalue){.z = query_product_owned(call->arguments[0].l)};
}
static jvalue is_item_owned_for_provider(const PortJniCall *call) {
    return (jvalue){.z = query_product_owned(call->arguments[1].l)};
}
static jvalue refresh_purchases(const PortJniCall *call) {
    (void)call;
    purchase_state.restore_pending = 1;
    return (jvalue){0};
}

static jvalue register_provider(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    char provider[256];
    port_jni_string_copy_utf8((jstring)arguments[0].l, provider, sizeof(provider));
    if (strcmp(provider, "Google") && strcmp(provider, "Amazon"))
        return (jvalue){0};
    jsize registered_count = port_jni->GetArrayLength(&port_jni, (jarray)arguments[1].l);
    for (jsize i = 0; i < registered_count; i++) {
        jstring product_id_string =
            (jstring)port_jni->GetObjectArrayElement(&port_jni, (jobjectArray)arguments[1].l, i);
        char id[PURCHASE_PRODUCT_ID_CAPACITY];
        port_jni_string_copy_utf8(product_id_string, id, sizeof(id));
        port_jni->DeleteLocalRef(&port_jni, product_id_string);
        if (!find_product(id)) {
            if (purchase_state.product_count == PURCHASE_PRODUCT_CAPACITY)
                port_fail("Purchase catalog too large");
            strcpy(purchase_state.products[purchase_state.product_count++].id, id);
        }
    }

    purchase_state.registration_pending = 1;
    return (jvalue){.z = port_app_options.unlock_all ? JNI_TRUE : JNI_FALSE};
}

static jvalue request_pending_purchases(const PortJniCall *call) {
    (void)call;
    purchase_state.delivery_pending = 1;
    return (jvalue){0};
}

static jvalue request_purchase(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_jni_string_copy_utf8((jstring)arguments[0].l, purchase_state.failed_product_id,
                              sizeof(purchase_state.failed_product_id));
    purchase_state.failure_pending = 1;
    PurchaseProduct *purchase_product = find_product(purchase_state.failed_product_id);
    purchase_state.response_status = purchase_product && purchase_product->is_owned
                                         ? PURCHASE_RESPONSE_SUCCESS
                                         : PURCHASE_RESPONSE_UNAVAILABLE;
    port_log_debug("Purchase request %s: %s", purchase_state.failed_product_id,
                   (purchase_state.response_status == PURCHASE_RESPONSE_SUCCESS)
                       ? ("restoring unlocked product")
                       : ("not owned; store purchase unavailable"));
    return (jvalue){0};
}

static const PortServiceMethod methods[] = {
    {{"com/miniclip/inapppurchases/MCInAppPurchases", "finishPurchase", "(Ljava/lang/String;)V", 1},
     finish_purchase},
    {{"com/miniclip/inapppurchases/MCInAppPurchases", "getItemCurrencyCode",
      "(Ljava/lang/String;)Ljava/lang/String;", 1},
     port_services_empty_text},
    {{"com/miniclip/inapppurchases/MCInAppPurchases", "getItemCurrencyCode",
      "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", 1},
     port_services_empty_text},
    {{"com/miniclip/inapppurchases/MCInAppPurchases", "getItemPrice",
      "(Ljava/lang/String;)Ljava/lang/String;", 1},
     port_services_empty_text},
    {{"com/miniclip/inapppurchases/MCInAppPurchases", "getItemPrice",
      "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", 1},
     port_services_empty_text},
    {{"com/miniclip/inapppurchases/MCInAppPurchases", "getItemPriceAmountMicros",
      "(Ljava/lang/String;)J", 1},
     port_services_empty_result},
    {{"com/miniclip/inapppurchases/MCInAppPurchases", "getItemPriceAmountMicros",
      "(Ljava/lang/String;Ljava/lang/String;)J", 1},
     port_services_empty_result},
    {{"com/miniclip/inapppurchases/MCInAppPurchases", "getPurchaseReceipt",
      "(Ljava/lang/String;)[Ljava/lang/String;", 1},
     purchase_receipt},
    {{"com/miniclip/inapppurchases/MCInAppPurchases", "isItemOwned", "(Ljava/lang/String;)Z", 1},
     is_item_owned},
    {{"com/miniclip/inapppurchases/MCInAppPurchases", "isItemOwned",
      "(Ljava/lang/String;Ljava/lang/String;)Z", 1},
     is_item_owned_for_provider},
    {{"com/miniclip/inapppurchases/MCInAppPurchases", "registerProvider",
      "(Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Z", 1},
     register_provider},
    {{"com/miniclip/inapppurchases/MCInAppPurchases", "requestPendingPurchases", "()V", 1},
     request_pending_purchases},
    {{"com/miniclip/inapppurchases/MCInAppPurchases", "requestPurchase", "(Ljava/lang/String;Z)V",
      1},
     request_purchase},
    {{"com/miniclip/inapppurchases/MCInAppPurchases", "restorePurchases", "()V", 1},
     refresh_purchases},
};
const PortServiceMethodTable port_purchases_methods = {methods,
                                                       sizeof(methods) / sizeof(methods[0])};
