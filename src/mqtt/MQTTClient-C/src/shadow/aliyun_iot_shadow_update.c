/*
 * aliyun_iot_shadow_update.c
 *
 *  Created on: May 17, 2017
 *      Author: qibiao.wqb
 */

#include "aliyun_iot_common_log.h"
#include "aliyun_iot_common_jsonparser.h"
#include "aliyun_iot_platform_timer.h"
#include "aliyun_iot_platform_pthread.h"
#include "aliyun_iot_device.h"

#include "shadow/aliyun_iot_shadow_update.h"


//add a new wait element
//return: NULL, failed; others, pointer of element.
aliot_update_ack_wait_list_pt aliyun_iot_shadow_update_wait_ack_list_add (
            aliot_shadow_pt pshadow,
            const char *ptoken, //NOTE: this is NOT a string.
            size_t token_len,
            aliot_update_cb_fpt cb,
            uint32_t timeout)
{
    int i;
    aliot_update_ack_wait_list_pt list = pshadow->inner_data.update_ack_wait_list;

    aliyun_iot_mutex_lock(&pshadow->mutex);

    for (i = 0; i < ALIOT_SHADOW_UPDATE_WAIT_ACK_LIST_NUM; ++i) {
        if (0 == list[i].flag_busy) {
            list[i].flag_busy = 1;
            break;
        }
    }

    aliyun_iot_mutex_unlock(&pshadow->mutex);

    if (i >= ALIOT_SHADOW_UPDATE_WAIT_ACK_LIST_NUM) {
        return NULL;
    }

    list[i].callback = cb;

    if (token_len >= ALIOT_SHADOW_TOKEN_LEN) {
        WRITE_IOT_WARNING_LOG("token is too long.");
        token_len = ALIOT_SHADOW_TOKEN_LEN - 1;
    }
    memcpy(list[i].token, ptoken, token_len);
    list[i].token[token_len] = '\0';

    InitTimer(&list[i].timer);
    countdown(&list[i].timer, timeout);

    WRITE_IOT_DEBUG_LOG("Add update ACK list");

    return &list[i];
}


void aliyun_iot_shadow_update_wait_ack_list_remove(aliot_shadow_pt pshadow, aliot_update_ack_wait_list_pt element)
{
    aliyun_iot_mutex_lock(&pshadow->mutex);
    element->flag_busy = 0;
    memset(element, 0, sizeof(aliot_update_ack_wait_list_t));
    aliyun_iot_mutex_unlock(&pshadow->mutex);
}


void aliyun_iot_shadow_update_wait_ack_list_handle_expire(aliot_shadow_pt pshadow)
{
    size_t i;

    aliot_update_ack_wait_list_pt pelement = pshadow->inner_data.update_ack_wait_list;

    aliyun_iot_mutex_lock(&pshadow->mutex);

    for (i = 0; i < ALIOT_SHADOW_UPDATE_WAIT_ACK_LIST_NUM; ++i) {
        if (0 != pelement[i].flag_busy) {
            if (expired(&pelement[i].timer)) {
                if (NULL != pelement[i].callback) {
                    pelement[i].callback(ALIOT_SHADOW_ACK_TIMEOUT, NULL, 0);
                }
                //free it.
                memset(&pelement[i], 0, sizeof(aliot_update_ack_wait_list_t));
            }
        }
    }

    aliyun_iot_mutex_unlock(&pshadow->mutex);
}


//handle response ACK of UPDATE
void aliyun_iot_shadow_update_wait_ack_list_handle_response(
            aliot_shadow_pt pshadow,
            char *json_doc,
            size_t json_doc_len)
{
    int data_len, payload_len, i;
    const char *pdata, *ppayload;
    aliot_update_ack_wait_list_pt pelement = pshadow->inner_data.update_ack_wait_list;

    //get token
    pdata = json_get_value_by_name(json_doc, (int)json_doc_len, "clientToken", &data_len, NULL);
    if (NULL == pdata) {
        WRITE_IOT_WARNING_LOG("Invalid JSON document: not 'clientToken' key");
        return;
    }

    ppayload = json_get_value_by_fullname(json_doc, (int)json_doc_len, "payload", &payload_len, NULL);
    if (NULL == ppayload) {
        WRITE_IOT_WARNING_LOG("Invalid JSON document: not 'payload' key");
        return;
    }

    aliyun_iot_mutex_lock(&pshadow->mutex);
    for (i = 0; i < ALIOT_SHADOW_UPDATE_WAIT_ACK_LIST_NUM; ++i) {

        if (0 != pelement[i].flag_busy) {
            //check the related
            if (0 == memcmp(pdata, pelement[i].token, strlen(pelement[i].token))) {

                aliyun_iot_mutex_unlock(&pshadow->mutex);
                WRITE_IOT_DEBUG_LOG("token=%s", pelement[i].token);
                do {
                    pdata = json_get_value_by_fullname(ppayload, payload_len, "status", &data_len, NULL);
                    if (NULL == pdata) {
                            WRITE_IOT_WARNING_LOG("Invalid JSON document: not 'payload.status' key");
                            break;
                    }

                    if (0 == strncmp(pdata, "success", data_len)) {
                        pelement[i].callback(ALIOT_SHADOW_ACK_SUCCESS, NULL, 0);
                    } else if (0 == strncmp(pdata, "error", data_len)){
                        aliot_shadow_ack_code_t ack_code;

                        pdata = json_get_value_by_fullname(ppayload, payload_len, "content.errorcode", &data_len, NULL);
                        if (NULL == pdata) {
                            WRITE_IOT_WARNING_LOG(
                                    "Invalid JSON document: not 'content.errorcode' key");
                            break;
                        }
                        ack_code = atoi(pdata);

                        pdata = json_get_value_by_fullname(ppayload, payload_len, "content.errormessage", &data_len, NULL);
                        if (NULL == pdata) {
                            WRITE_IOT_WARNING_LOG(
                                    "Invalid JSON document: not 'content.errormessage' key");
                            break;
                        }

                        WRITE_IOT_WARNING_LOG("###############1");
                        pelement[i].callback(ack_code, pdata, data_len);
                        WRITE_IOT_WARNING_LOG("###############2");
                    } else {
                        WRITE_IOT_WARNING_LOG(
                                "Invalid JSON document: value of 'status' key is invalid.");
                    }
                } while(0);

                aliyun_iot_mutex_lock(&pshadow->mutex);
                memset(&pelement[i], 0, sizeof(aliot_update_ack_wait_list_t));
                aliyun_iot_mutex_unlock(&pshadow->mutex);
                return;
            }
        }
    }

    WRITE_IOT_WARNING_LOG("Not match any wait element in list.");
}
