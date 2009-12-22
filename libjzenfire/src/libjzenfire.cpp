
//############################################################################//

/** \file libjzenfire.cpp
 * \brief JNI to libzenfire
 */

// L I C E N S E #############################################################//

/*
 *  Copyright 2009 BigWells Technology (Zen-Fire)
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

// I N C L U D E S ###########################################################//

#include <jni.h>

#include <zenfire/client.hpp>
#include <zenfire/error.hpp>
#include <zenfire/arg.hpp>
#include <zenfire/product.hpp>

#include <iostream>
#include <sstream>
#include <ctime>
#include <cstdlib>
#include <string>

using namespace std;

JavaVM *the_vm;
jclass ClientImpl;
jmethodID invokeCallback_tick;
jmethodID invokeCallback_report;
jmethodID invokeCallback_alert;

jclass OutOfMemoryError;
jclass AccessException;
jclass ConnectionException;
jclass TimeoutException;
jclass InvalidException;
jclass InvalidAccountException;
jclass InvalidInstrumentException;
jclass InternalException;
jclass ZenFireException;
jclass String;
jclass Instrument;
jmethodID Instrument_init;
jclass Instrument_Specification;
jmethodID Instrument_Specification_init;
jclass BigDecimal;
jmethodID BigDecimal_init;
jclass MathContext;
jmethodID MathContext_init;

extern "C" jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    the_vm = vm;
    return JNI_VERSION_1_4;
}

class jni_exception : public std::runtime_error {
    public:
    jni_exception() : std::runtime_error("An unexpected JNI error occurred") { }
};

class env_attachment {
    private:
    bool detach;
    JNIEnv *envp;

    public:
    env_attachment() {
        jint result = the_vm->GetEnv((void **) &envp, JNI_VERSION_1_4);
        if (result == JNI_EDETACHED) {
            detach = true;
            the_vm->AttachCurrentThread((void **) &envp, NULL);
        } else if (result == JNI_OK) {
            detach = false;
        } else {
            throw jni_exception();
        }
    }

    ~env_attachment() {
        if (detach) {
            the_vm->DetachCurrentThread();
        }
    }

    JNIEnv *env() { return envp; }
};

class global_ref {
    private:
    jobject grobj;

    public:
    global_ref(jobject obj) {
        if (obj == NULL) { grobj == NULL; return; }
        env_attachment a;
        grobj = a.env()->NewGlobalRef(obj);
        if (grobj == NULL) {
            throw jni_exception();
        }
    }

    global_ref(JNIEnv *env, jobject obj) {
        if (obj == NULL) { grobj == NULL; return; }
        grobj = env->NewGlobalRef(obj);
        if (grobj == NULL) {
            throw jni_exception();
        }
    }

    global_ref(const global_ref &o) {
        if (o.grobj == NULL) {
            grobj = NULL;
        } else {
            env_attachment a;
            grobj = a.env()->NewGlobalRef(o.grobj);
        }
    }

    global_ref& operator=(const global_ref &o) {
        if (this == &o || grobj == NULL && o.grobj == NULL) {
            return *this;
        }
        env_attachment a;
        if (grobj != NULL) {
            a.env()->DeleteGlobalRef(grobj);
        }
        if (o.grobj == NULL) {
            grobj = NULL;
        } else {
            grobj = a.env()->NewGlobalRef(o.grobj);
            if (grobj == NULL) {
                throw jni_exception();
            }
        }
        return *this;
    }

    jobject obj() { return grobj; }

    ~global_ref() {
        if (grobj != NULL) {
            env_attachment a;
            a.env()->DeleteGlobalRef(grobj);
        }
    }
};

class tick_callback_t {

    private:
    global_ref obj;

    public:
    tick_callback_t(global_ref obj) : obj(obj) {}

    ~tick_callback_t() { }

    void operator()(const zenfire::tick::tick_t& tick) {
        env_attachment a;

        jstring symbol = a.env()->NewStringUTF(tick.product->symbol.c_str());
        jstring exchange = a.env()->NewStringUTF(zenfire::exchange::to_string(tick.product->exchange).c_str());

        a.env()->CallVoidMethod(obj.obj(),
            invokeCallback_tick,
            (jint) tick.typ_,
            (jint) 0,
            (jobject) symbol,
            (jobject) exchange,
            ((jlong)tick.ts) * 1000L + ((jlong)tick.usec / 1000L), // millis
            (((jint)tick.usec) % 1000) * 1000, // nanos
            (jdouble) tick.price,
            (jint) tick.size);
    }
};

class alert_callback_t {

    private:
    global_ref obj;

    public:
    alert_callback_t(global_ref obj) : obj(obj) {}

    ~alert_callback_t() { }

    void operator()(const zenfire::alert::alert_t& alert) {
        env_attachment a;

        jstring message = a.env()->NewStringUTF(alert.message().c_str());

        a.env()->CallVoidMethod(obj.obj(),
            invokeCallback_alert,
            (jint) alert.type(),
            (jint) alert.number(),
            (jobject) message);
    }
};

class report_callback_t {

    private:
    global_ref obj;

    public:
    report_callback_t(global_ref obj) : obj(obj) { }

    ~report_callback_t() { }

    void operator()(const zenfire::report::report_t& report) {
        env_attachment a;

        jstring message = a.env()->NewStringUTF(report.message().c_str());

        a.env()->CallVoidMethod(obj.obj(),
            invokeCallback_report,
            (jint) report.typ_,
            (jobject) message,
            (jint) report.qty(),
            (jdouble) report.price(),
            (jlong) new zenfire::order_ptr(report.order),
            ((jlong)report.ts) * 1000L + ((jlong)report.usec / 1000L), // millis
            (((jint)report.usec) % 1000) * 1000 // nanos
        );
    }
};

void throw_java(JNIEnv *env, exception *ex) {
    jclass extype = ZenFireException;
    if (dynamic_cast<zenfire::error::access_t*>(ex) != 0) {
        extype = AccessException;
    } else if (dynamic_cast<zenfire::error::connection_t*>(ex) != 0) {
        extype = ConnectionException;
    } else if (dynamic_cast<zenfire::error::timeout_t*>(ex) != 0) {
        extype = TimeoutException;
    } else if (dynamic_cast<zenfire::error::invalid_t*>(ex) != 0) {
        extype = InvalidException;
    } else if (dynamic_cast<zenfire::error::invalid_account_t*>(ex) != 0) {
        extype = InvalidAccountException;
    } else if (dynamic_cast<zenfire::error::invalid_product_t*>(ex) != 0) {
        extype = InvalidInstrumentException;
    } else if (dynamic_cast<zenfire::error::internal_t*>(ex) != 0) {
        extype = InternalException;
    }
    const char *what = ex->what();
    if (! what) {
        what = "(no message)";
    }
    env->ThrowNew(extype, what);
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_init0(JNIEnv *env, jclass clazz) {
    ClientImpl = clazz;
    invokeCallback_tick = env->GetMethodID(clazz, "invokeCallback", "(IILjava/lang/String;Ljava/lang/String;JIDI)V");
    invokeCallback_alert = env->GetMethodID(clazz, "invokeCallback", "(IILjava/lang/String;)V");
    invokeCallback_report = env->GetMethodID(clazz, "invokeCallback", "(ILjava/lang/String;IDJJI)V");
    OutOfMemoryError = (jclass) env->NewGlobalRef(env->FindClass("java/lang/OutOfMemoryError"));
    AccessException = (jclass) env->NewGlobalRef(env->FindClass("jzenfire/AccessException"));
    ConnectionException = (jclass) env->NewGlobalRef(env->FindClass("jzenfire/ConnectionException"));
    TimeoutException = (jclass) env->NewGlobalRef(env->FindClass("jzenfire/TimeoutException"));
    InvalidException = (jclass) env->NewGlobalRef(env->FindClass("jzenfire/InvalidException"));
    InvalidAccountException = (jclass) env->NewGlobalRef(env->FindClass("jzenfire/InvalidAccountException"));
    InvalidInstrumentException = (jclass) env->NewGlobalRef(env->FindClass("jzenfire/InvalidInstrumentException"));
    InternalException = (jclass) env->NewGlobalRef(env->FindClass("jzenfire/InternalException"));
    ZenFireException = (jclass) env->NewGlobalRef(env->FindClass("jzenfire/ZenFireException"));
    String = (jclass) env->NewGlobalRef(env->FindClass("java/lang/String"));
    Instrument = (jclass) env->NewGlobalRef(env->FindClass("jzenfire/Instrument"));
    Instrument_init = env->GetMethodID(Instrument, "<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/math/BigDecimal;ILjzenfire/Instrument$Specification;)V");
    Instrument_Specification = (jclass) env->NewGlobalRef(env->FindClass("jzenfire/Instrument$Specification"));
    Instrument_Specification_init = env->GetMethodID(Instrument_Specification, "<init>", "(ILjava/math/BigDecimal;Ljava/lang/String;Ljava/lang/String;)V");
    BigDecimal = (jclass) env->NewGlobalRef(env->FindClass("java/math/BigDecimal"));
    BigDecimal_init = env->GetMethodID(BigDecimal, "<init>", "(DLjava/math/MathContext;)V");
    MathContext = (jclass) env->NewGlobalRef(env->FindClass("java/math/MathContext"));
    MathContext_init = env->GetMethodID(MathContext, "<init>", "(I)V");
}

string to_string(JNIEnv *env, jstring jstr) {
    if (jstr == NULL) {
        return string("");
    }
    const char *cchars = env->GetStringUTFChars(jstr, NULL);
    string newstr(cchars, env->GetStringUTFLength(jstr));
    env->ReleaseStringUTFChars(jstr, cchars);
    return newstr;
}

string to_string(JNIEnv *env, jcharArray jca) {
    int len = env->GetArrayLength(jca);
    jchar *jca_chars = env->GetCharArrayElements(jca, NULL);
    char chars[len+1];
    for (int i = 0; i < len; i ++) {
        // truncate to 8 bit chars
        chars[i] = (char) jca_chars[i];
    }
    env->ReleaseCharArrayElements(jca, jca_chars, JNI_ABORT);
    chars[len] = 0;
    return string(chars);
}

extern "C" JNIEXPORT jlong JNICALL Java_jzenfire_ClientImpl_create0(JNIEnv *env, jclass clazz, jobject clientImpl, jstring path) {
    jlong ptr = 0L;
    try {
        zenfire::client::client_t *client = zenfire::client::create(to_string(env, path));
        ptr = (jlong) client;
        client->hook_alerts(alert_callback_t(global_ref(env, clientImpl)));
        client->hook_reports(report_callback_t(global_ref(env, clientImpl)));
        client->hook_ticks(tick_callback_t(global_ref(env, clientImpl)));
    } catch (exception &ex) {
        throw_java(env, &ex);
        return 0L;
    }
    return ptr;
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_free0(JNIEnv *env, jclass clazz, jlong ptr) {
    delete ((zenfire::client_t *)ptr);
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_login0(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jstring user,
    jcharArray passwd,
    jstring environment) {

    zenfire::client_t *zf = (zenfire::client_t *)ptr;

    string user_str = to_string(env, user);
    string environment_str = to_string(env, environment);
    string passwd_str = to_string(env, passwd);

    try {
        zf->login(user_str, passwd_str, environment_str);
    } catch (exception &ex) {
        throw_java(env, &ex);
    }

}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_logout0(JNIEnv *env, jclass clazz, jlong ptr) {
    zenfire::client_t *zf = (zenfire::client_t *)ptr;

    try {
        zf->logout();
    } catch (exception &ex) {
        throw_java(env, &ex);
    }
}

extern "C" JNIEXPORT jint JNICALL Java_jzenfire_ClientImpl_getOption0(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jstring option) {

    zenfire::client_t *zf = (zenfire::client_t *)ptr;

    string option_str = to_string(env, option);

    try {
        return (jint) zf->option(option_str);
    } catch (exception &ex) {
        throw_java(env, &ex);
        return 0;
    }
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_setOption0(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jstring option,
    jint value) {

    zenfire::client_t *zf = (zenfire::client_t *)ptr;

    string option_str = to_string(env, option);

    try {
        zf->option(option_str, value);
    } catch (exception &ex) {
        throw_java(env, &ex);
    }
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_jzenfire_ClientImpl_getEnvironments0(
    JNIEnv *env,
    jclass clazz,
    jlong ptr) {

    zenfire::client_t *zf = (zenfire::client_t *)ptr;

    vector<string> envvec;

    try {
        envvec = zf->list_environments();
    } catch (exception &ex) {
        throw_java(env, &ex);
        return NULL;
    }

    int len = envvec.size();

    jobjectArray stringArray = env->NewObjectArray(len, String, NULL);

    for (int i = 0; i < len; i ++) {
        env->SetObjectArrayElement(stringArray, (jsize) i, (jobject) env->NewStringUTF(envvec[i].c_str()));
    }

    return stringArray;
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_jzenfire_ClientImpl_getAccounts0(
    JNIEnv *env,
    jclass clazz,
    jlong ptr) {

    zenfire::client_t *zf = (zenfire::client_t *)ptr;

    vector<string> actvec;

    try {
        actvec = zf->list_accounts();
    } catch (exception &ex) {
        throw_java(env, &ex);
        return NULL;
    }

    int len = actvec.size();

    jobjectArray stringArray = env->NewObjectArray(len, String, NULL);

    for (int i = 0; i < len; i ++) {
        env->SetObjectArrayElement(stringArray, (jsize) i, (jobject) env->NewStringUTF(actvec[i].c_str()));
    }

    return stringArray;
}

extern "C" JNIEXPORT jint JNICALL Java_jzenfire_ClientImpl_lookupAccount0(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jstring name) {

    zenfire::client_t *zf = (zenfire::client_t *)ptr;

    string name_str = to_string(env, name);

    try {
        return zf->lookup_account(name_str);
    } catch (exception &ex) {
        throw_java(env, &ex);
        return 0;
    }
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_subscribeAccount0(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jint acctno,
    jint flags) {

    zenfire::client_t *zf = (zenfire::client_t *)ptr;

    try {
        zf->subscribe_account(acctno, flags);
    } catch (exception &ex) {
        throw_java(env, &ex);
    }
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_unsubscribeAccount0(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jint acctno) {

    zenfire::client_t *zf = (zenfire::client_t *)ptr;

    try {
        zf->unsubscribe_account(acctno);
    } catch (exception &ex) {
        throw_java(env, &ex);
    }
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_replayOpenOrders0(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jint acctno) {

    zenfire::client_t *zf = (zenfire::client_t *)ptr;

    try {
        zf->request_open_orders(acctno);
    } catch (exception &ex) {
        throw_java(env, &ex);
    }
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_replayOrders0(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jint acctno,
    jint from,
    jint to) {

    zenfire::client_t *zf = (zenfire::client_t *)ptr;

    try {
        zf->request_orders(from, to, acctno);
    } catch (exception &ex) {
        throw_java(env, &ex);
    }
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_replayProfitLoss0(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jint acctno) {

    zenfire::client_t *zf = (zenfire::client_t *)ptr;

    try {
        zf->request_pl(acctno);
    } catch (exception &ex) {
        throw_java(env, &ex);
    }
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_replayPositions0(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jint acctno) {

    zenfire::client_t *zf = (zenfire::client_t *)ptr;

    try {
        zf->request_positions(acctno);
    } catch (exception &ex) {
        throw_java(env, &ex);
    }
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_cancelAll0(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jint acctno) {

    zenfire::client_t *zf = (zenfire::client_t *)ptr;

    try {
        zf->cancel_all(acctno);
    } catch (exception &ex) {
        throw_java(env, &ex);
    }
}

jobject createInstrument(JNIEnv *env, const zenfire::product::product_t &prod) {
    jint precision = prod.precision;
    jobject mathContext = env->NewObject(MathContext, MathContext_init, (jint) precision);
    if (env->ExceptionCheck()) return NULL;

    jobject spec;
    if (prod.has_specs) {
        jobject pointValue = env->NewObject(BigDecimal, BigDecimal_init, prod.point_value, mathContext);
        if (env->ExceptionCheck()) return NULL;
        jobject currency = env->NewStringUTF(prod.currency.c_str());
        if (env->ExceptionCheck()) return NULL;
        jobject description = env->NewStringUTF(prod.description.c_str());
        if (env->ExceptionCheck()) return NULL;
        spec = env->NewObject(Instrument_Specification, Instrument_Specification_init, precision, pointValue, currency, description);
        if (env->ExceptionCheck()) return NULL;
    } else {
        spec = NULL;
    }
    jstring symbol = env->NewStringUTF(prod.symbol.c_str());
    if (env->ExceptionCheck()) return NULL;
    jstring exchange = env->NewStringUTF(zenfire::exchange::to_string(prod.exchange).c_str());
    if (env->ExceptionCheck()) return NULL;

    jobject tickSize = env->NewObject(BigDecimal, BigDecimal_init, (jdouble) prod.increment, mathContext);
    if (env->ExceptionCheck()) return NULL;
    jobject inst = env->NewObject(Instrument, Instrument_init, symbol, exchange, tickSize, precision, spec);
    if (env->ExceptionCheck()) return NULL;
    return inst;
}

extern "C" JNIEXPORT jobject JNICALL Java_jzenfire_ClientImpl_lookupInstrument0(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jstring symbol,
    jstring exchange) {

    zenfire::client_t *zf = (zenfire::client_t *)ptr;

    string symbol_str = to_string(env, symbol);
    string exchange_str = to_string(env, exchange);

    zenfire::product::product_t prod;

    try {
        prod = zf->lookup_product(zenfire::arg::product(symbol_str, exchange_str));
    } catch (exception &ex) {
        throw_java(env, &ex);
        return NULL;
    }

    return createInstrument(env, prod);
}

extern "C" JNIEXPORT jlong JNICALL Java_jzenfire_ClientImpl_placeOrder0(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jint type,
    jdouble limitPrice,
    jdouble triggerPrice,
    jstring acctName,
    jstring symbol,
    jstring exchange,
    jint action,
    jint qty,
    jint duration,
    jobject order,
    jstring zentag,
    jstring tag) {

    zenfire::client_t *zf = (zenfire::client_t *)ptr;
    // pointer to a shared pointer to an order, heh
    zenfire::order_ptr *optr;

    zenfire::arg::market args = zenfire::arg::market();

    int account_number;
    try {
        account_number = zf->lookup_account(to_string(env, acctName));
    } catch (exception &ex) {
        throw_java(env, &ex);
        return NULL;
    }

    args.product = zf->lookup_product(zenfire::arg::product(to_string(env, symbol), to_string(env, exchange)));
    args.action = (zenfire::order::action_t) action;
    args.qty = (int) qty;
    args.duration = (zenfire::order::duration_t) duration;
    args.zentag = to_string(env, zentag);
    args.tag = to_string(env, tag);

    try {
        switch (type) {
            case 1: {
                optr = new zenfire::order_ptr(zf->place_order(args, account_number));
                break;
            }
            case 2: {
                optr = new zenfire::order_ptr(zf->place_order(zenfire::arg::limit(limitPrice, args), account_number));
                break;
            }
            case 3: {
                optr = new zenfire::order_ptr(zf->place_order(zenfire::arg::stop_market(triggerPrice, args), account_number));
                break;
            }
            case 4: {
                optr = new zenfire::order_ptr(zf->place_order(zenfire::arg::stop_limit(triggerPrice, zenfire::arg::limit(limitPrice, args)), account_number));
                break;
            }
        }
    } catch (exception &ex) {
        throw_java(env, &ex);
        return NULL;
    }
    return (jlong) optr;
}

extern "C" JNIEXPORT jlong JNICALL Java_jzenfire_ClientImpl_prepareOrder0(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jint type,
    jdouble limitPrice,
    jdouble triggerPrice,
    jstring acctName,
    jstring symbol,
    jstring exchange,
    jint action,
    jint qty,
    jint duration,
    jobject order,
    jstring zentag,
    jstring tag) {

    zenfire::client_t *zf = (zenfire::client_t *)ptr;
    // pointer to a shared pointer to an order, heh
    zenfire::order_ptr *optr;

    zenfire::arg::market args = zenfire::arg::market();

    int account_number;
    try {
        account_number = zf->lookup_account(to_string(env, acctName));
    } catch (exception &ex) {
        throw_java(env, &ex);
        return NULL;
    }

    args.product = zf->lookup_product(zenfire::arg::product(to_string(env, symbol), to_string(env, exchange)));
    args.action = (zenfire::order::action_t) action;
    args.qty = (int) qty;
    args.duration = (zenfire::order::duration_t) duration;
    args.zentag = to_string(env, zentag);
    args.tag = to_string(env, tag);

    try {
        switch (type) {
            case 1: {
                optr = new zenfire::order_ptr(zf->prepare_order(args, account_number));
                break;
            }
            case 2: {
                optr = new zenfire::order_ptr(zf->prepare_order(zenfire::arg::limit(limitPrice, args), account_number));
                break;
            }
            case 3: {
                optr = new zenfire::order_ptr(zf->prepare_order(zenfire::arg::stop_market(triggerPrice, args), account_number));
                break;
            }
            case 4: {
                optr = new zenfire::order_ptr(zf->prepare_order(zenfire::arg::stop_limit(triggerPrice, zenfire::arg::limit(limitPrice, args)), account_number));
                break;
            }
        }
    } catch (exception &ex) {
        throw_java(env, &ex);
        return NULL;
    }
    return (jlong) optr;
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_replayTicks0(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jstring symbol,
    jstring exchange,
    jint from,
    jint to) {

    zenfire::client_t *zf = (zenfire::client_t *)ptr;

    try {
        zenfire::product_t product = zf->lookup_product(zenfire::arg::product(to_string(env, symbol), to_string(env, exchange)));
        zf->replay_ticks(product, from, to);
    } catch (exception &ex) {
        throw_java(env, &ex);
    }
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_subscribe0(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jstring symbol,
    jstring exchange,
    jint flags) {

    zenfire::client_t *zf = (zenfire::client_t *)ptr;

    try {
        zenfire::product_t product = zf->lookup_product(zenfire::arg::product(to_string(env, symbol), to_string(env, exchange)));
        zf->subscribe(product, (uint32_t) flags);
    } catch (exception &ex) {
        throw_java(env, &ex);
    }
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_unsubscribe0(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jstring symbol,
    jstring exchange) {

    zenfire::client_t *zf = (zenfire::client_t *)ptr;

    try {
        zenfire::product_t product = zf->lookup_product(zenfire::arg::product(to_string(env, symbol), to_string(env, exchange)));
        zf->unsubscribe(product);
    } catch (exception &ex) {
        throw_java(env, &ex);
    }
}

extern "C" JNIEXPORT jint JNICALL Java_jzenfire_ClientImpl_orderGetStatus0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;

    return (jint) (*orderpp)->status();
}

extern "C" JNIEXPORT jstring JNICALL Java_jzenfire_ClientImpl_orderGetMessage0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;

    return env->NewStringUTF((*orderpp)->message().c_str());
}

extern "C" JNIEXPORT jstring JNICALL Java_jzenfire_ClientImpl_orderGetAccountName0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;

    return env->NewStringUTF((*orderpp)->acct().c_str());
}

extern "C" JNIEXPORT jdouble JNICALL Java_jzenfire_ClientImpl_orderGetAvgFillPrice0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;
    return (jdouble) (*orderpp)->fill_price();
}

extern "C" JNIEXPORT jint JNICALL Java_jzenfire_ClientImpl_orderGetDuration0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;
    return (jint) (*orderpp)->duration();
}

extern "C" JNIEXPORT jstring JNICALL Java_jzenfire_ClientImpl_orderGetExchange0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;

    return env->NewStringUTF(zenfire::exchange::to_string((*orderpp)->product().exchange).c_str());
}

extern "C" JNIEXPORT jstring JNICALL Java_jzenfire_ClientImpl_orderGetSymbol0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;

    return env->NewStringUTF((*orderpp)->product().symbol.c_str());
}

extern "C" JNIEXPORT jint JNICALL Java_jzenfire_ClientImpl_orderGetType0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;
    return (jint) (*orderpp)->type();
}

extern "C" JNIEXPORT jdouble JNICALL Java_jzenfire_ClientImpl_orderGetLimitPrice0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;
    return (jdouble) (*orderpp)->price();
}

extern "C" JNIEXPORT jint JNICALL Java_jzenfire_ClientImpl_orderGetQty0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;
    return (jint) (*orderpp)->qty();
}

extern "C" JNIEXPORT jint JNICALL Java_jzenfire_ClientImpl_orderGetSide0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;
    return (jint) (*orderpp)->action();
}

extern "C" JNIEXPORT jstring JNICALL Java_jzenfire_ClientImpl_orderGetTag0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;

    return env->NewStringUTF((*orderpp)->tag().c_str());
}

extern "C" JNIEXPORT jdouble JNICALL Java_jzenfire_ClientImpl_orderGetTriggerPrice0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;
    return (jdouble) (*orderpp)->trigger();
}

extern "C" JNIEXPORT jstring JNICALL Java_jzenfire_ClientImpl_orderGetZenTag0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;

    return env->NewStringUTF((*orderpp)->zentag().c_str());
}

extern "C" JNIEXPORT jint JNICALL Java_jzenfire_ClientImpl_orderGetReason0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;
    return (jint) (*orderpp)->reason();
}

extern "C" JNIEXPORT jint JNICALL Java_jzenfire_ClientImpl_orderGetNumber0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;
    return (jint) (*orderpp)->number();
}

extern "C" JNIEXPORT jint JNICALL Java_jzenfire_ClientImpl_orderGetQtyOpen0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;

    return (jint) (*orderpp)->open();
}

extern "C" JNIEXPORT jint JNICALL Java_jzenfire_ClientImpl_orderGetQtyFilled0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;

    return (jint) (*orderpp)->filled();
}

extern "C" JNIEXPORT jint JNICALL Java_jzenfire_ClientImpl_orderGetQtyCancelled0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;

    return (jint) (*orderpp)->canceled();
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_orderSetSetPrice0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr,
    jdouble price) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;

    try {
        (*orderpp)->set_price((double)price);
    } catch (exception &ex) {
        throw_java(env, &ex);
    }
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_orderSetSetQty0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr,
    jint qty) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;

    try {
        (*orderpp)->set_qty((int)qty);
    } catch (exception &ex) {
        throw_java(env, &ex);
    }
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_orderSetSetTrigger0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr,
    jdouble trigger) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;

    try {
        (*orderpp)->set_trigger((double)trigger);
    } catch (exception &ex) {
        throw_java(env, &ex);
    }
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_orderSend0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;

    try {
        (*orderpp)->send();
    } catch (exception &ex) {
        throw_java(env, &ex);
    }
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_orderUpdate0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;

    try {
        (*orderpp)->update();
    } catch (exception &ex) {
        throw_java(env, &ex);
    }
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_orderCancel0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr,
    jstring reason) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;

    try {
        (*orderpp)->cancel(to_string(env, reason));
    } catch (exception &ex) {
        throw_java(env, &ex);
    }
}

extern "C" JNIEXPORT jobject JNICALL Java_jzenfire_ClientImpl_orderGetInstrument0(
    JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;

    try {
        zenfire::product::product_t product = (*orderpp)->product();
        return createInstrument(env, product);
    } catch (exception &ex) {
        throw_java(env, &ex);
    }
}

extern "C" JNIEXPORT void JNICALL Java_jzenfire_ClientImpl_orderFree0(JNIEnv *env,
    jclass clazz,
    jlong orderPtr) {

    zenfire::order_ptr *orderpp = (zenfire::order_ptr *)orderPtr;
    delete orderpp;
}

//############################################################################//
