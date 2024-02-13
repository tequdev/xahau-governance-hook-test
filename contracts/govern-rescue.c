#include "hookapi.h"

#define ASSERT(x)\
    if (!(x))\
        rollback(SBUF("Govern: Assertion failed."),__LINE__);

#define HOOK_MAX 10 // maximum number of hooks on an account

/**
 * Xahau Governance Rescue Hook
 *
 *  The governance model is a 20 seat round table.
 *  Each seat may be filled or empty (provided there is at least one filled seat.)
 *  The primary governance table sits on the genesis account. This is also called the L1 table.
 *  Seats at this table are called L1s or L1 members.
 *  At L1, for votes relating to table membership 80% of the filled seats must vote in favour.
 *  At L1, for votes relating to everything else 100% of the filled seats must vote in favour.
 *  One or more L1 seats may contain an account that has an L2 governance hook installed on it and is blackholed.
 *  This is referred to as an L2 table. The seats at the table are called L2s or L2 members.
 *  There may be multiple L2 tables.
 *
 *  Topics:
 *      'H[0-9]'    - Hook Hash in positions 0-9 <32 byte hash> on genesis
 *
 *  Namespace: Shoud match Governance hook's namespace.
 *
 *  Hook State:
 *
 *      State Key: {'V', 'H' <topic type>, '\0 + topic id', <layer>,  0..0, <member accid>}
 *      State Data: A vote by a member for a topic and topic data
 *
 *      State Key: {'C', 'H' <topic type>, '\0 + topic id', <layer>, 0*, <front truncated topic data>}
 *      State Data: The number of members who have voted for that topic data and topic combination <1 byte>
 *
 *  Hook Invocation:
 *      ttINVOKE:
 *          First time:
 *              Behaviour: Setup hook, setup initial accounts, end (accept).
 *
 *          Subsequent:
 *              Behaviour: Vote on a topic, if the votes meet the topic vote threshold, action the topic.
 *
 *              Parameter Name: {'L'}
 *              Parameter Value: Which layer the vote is inteded for (ONLY L2 TABLES USE THIS PARAMETER)
 *                  { 1 a vote cast by an L2 member about an L1 topic }, or
 *                  { 2 a vote cast by an L2 member about an L2 topic }
 *
 *
 *              Parameter Name: {'T'}
 *              Parameter Value: The topic to vote on <2 bytes>
 *                  { '|H' (hook), '\0 + topic id' }
 *
 *              Parameter Name: {'V'}
 *              Parameter Value: The data to vote for this topic (hook hash)
 **/

#define SVAR(x) &x, sizeof(x)

#define DONE(x)\
    accept(SBUF(x),__LINE__);

#define NOPE(x)\
    rollback(SBUF(x), __LINE__);

#define DEBUG 1

// genesis account id
uint8_t genesis[20] =
    {0xB5U,0xF7U,0x62U,0x79U,0x8AU,0x53U,0xD5U,0x43U,0xA0U,0x14U,
     0xCAU,0xF8U,0xB2U,0x97U,0xCFU,0xF8U,0xF2U,0xF9U,0x37U,0xE8U};

uint8_t zero32[32];

int64_t hook(uint32_t r)
{
    _g(1,1);

    etxn_reserve(1);

    // in debug mode the test case can supply a line number that the hook will then print here.
    {
        uint8_t ln[2];
        if (otxn_param(SBUF(ln), "D", 1) == 2)
        {
            uint16_t lineno = (((uint16_t)ln[0]) << 8U) + ln[1];
            trace_num(SBUF("DBGLN"), lineno);
        }
    }

    int64_t tt = otxn_type();

    if (tt != ttPAYMENT)  // ttPayment only
        DONE("Governance Rescue: Passing non-Payment txn. HookOn should be changed to avoid this.");
        
    otxn_slot(1);
    slot_subfield(1, sfAmount, 1);

    if(slot_type(1, 1) != 1)
      DONE("Non-native currency. Passing.");
    if (float_int(slot_float(1), 6, 0) > 1)
      DONE("Only 1 drop allowed. Passing.");

    // get the account id
    uint8_t account_field[32];
    otxn_field(account_field + 12, 20, sfAccount);

    uint8_t hook_accid[32];
    hook_account(hook_accid + 12, 20);

    // outgoing txns to other hooks allowed
    if (BUFFER_EQUAL_20(hook_accid + 12, account_field + 12))
    {
        uint8_t dest_acc[20];
        if (otxn_field(SBUF(dest_acc), sfDestination) == 20 && !BUFFER_EQUAL_20(hook_accid + 12, dest_acc))
            DONE("Goverance: Passing outgoing txn.");
    }
    
    int64_t is_L1_table = BUFFER_EQUAL_20(hook_accid + 12, genesis);

    if (is_L1_table)
        trace(SBUF("Governance: Starting governance logic on L1 table."), 0,0,0);
    else
        trace(SBUF("Governance: Starting governance logic on L2 table."), 0,0,0);

    int64_t member_count = state(0,0, "MC", 2);
   
    // initial execution, setup hook
    if (member_count == DOESNT_EXIST)
    {
        DONE("Governance: Setup has not been done.");
    }

    // otherwise a normal execution (not initial)
    // first let's check if the invoking party is a member
    
    int64_t member_id = state(0,0,account_field + 12, 20);
    if (member_id < 0)
        NOPE("Governance: You are not currently a governance member at this table.");

    // the only thing a member can do is vote for a topic
    // so lets process their vote
  
    // { 'S|H|R', '\0 + topicid' }
    uint8_t topic[2];
    int64_t result = otxn_param(SBUF(topic), "T", 1);
    uint8_t t = topic[0];   // topic type
    uint8_t n = topic[1];   // number (seats) (or R/D for reward rate/delay)

    if (result != 2 || (
                t != 'S' &&      // topic type: seat 
                t != 'H' &&      // topic type: hook 
                t != 'R'))       // topic type: reward 
        NOPE("Governance: Valid TOPIC must be specified as otxn parameter.");

   
    if (t == 'S')
        DONE("Governance: Seat topics are not allowed.");

    if (t == 'H' && n > HOOK_MAX)
        NOPE("Governance: Valid hook topics are 0 through 9.");

    if (t == 'R')
        DONE("Governance: Reward topics are not allowed");
   
    // is their vote for the L2 table or the L1 table?
    uint8_t l = 1;
    if (!is_L1_table)
    {
        result = otxn_param(&l, 1, "L", 1);
        if (result != 1)
            NOPE("Governance: Missing L parameter. Which layer are you voting for?");

        TRACEVAR(l);   
 
        if (l != 1 && l != 2)
            NOPE("Governance: Layer parameter must be '1' or '2'.");
    }

    // if (l == 2 && t == 'R')
    //     NOPE("Governance: L2s cannot vote on RR/RD at L2, did you mean to set L=1?");


    // RH TODO: validate RR/RD xfl > 0
    uint8_t topic_data[56 /* there's a 24 byte pad on the back for later logic */];
    uint8_t topic_size = 32;  // hook topics are a 32 byte hook hash
        // t == 'H' ? 32 :      // hook topics are a 32 byte hook hash
        // t == 'S' ? 20 :      // account topics are a 20 byte account ID
        //             8;       // reward topics are an 8 byte le xfl
    
    uint8_t padding = 32 - topic_size;

    result = otxn_param(topic_data + padding, topic_size, "V", 1);
    if (result != topic_size)
        NOPE("Governance: Missing or incorrect size of VOTE data for TOPIC type.");

    // set this flag if the topic data is all zeros
    int topic_data_zero = 
        (*((uint64_t*)(topic_data +  0)) == 0) &&
        (*((uint64_t*)(topic_data +  8)) == 0) &&
        (*((uint64_t*)(topic_data + 16)) == 0) &&
        (*((uint64_t*)(topic_data + 24)) == 0);

    trace(SBUF("topic_data_raw:"), topic_data, 56, 1);
    trace_num(SBUF("topic_padding:"), padding);
    trace_num(SBUF("topic_size:"), topic_size);
    trace(SBUF("topic_data:"), topic_data + padding, topic_size, 1);

    // reuse account_field to create vote key
    account_field[0] = 'V';
    account_field[1] = t;
    account_field[2] = n;
    account_field[3] = l;

    // get their previous vote if any on this topic
    uint8_t previous_topic_data[32];
    int64_t previous_topic_size =
        state(previous_topic_data + padding, topic_size, SBUF(account_field));

    // check if the vote they're making has already been cast before,
    // if it is identical to their existing vote for this topic then just end with tesSUCCESS
    trace(SBUF("previous_topic_data"), previous_topic_data, 32, 1);
    trace(SBUF("topic_data"), topic_data, 32, 1);
    trace_num(SBUF("previous_topic_size"), previous_topic_size);
    trace_num(SBUF("topic_size"), topic_size);
    if (previous_topic_size == topic_size && BUFFER_EQUAL_32(previous_topic_data, topic_data))
        DONE("Governance: Your vote is already cast this way for this topic.");

    // execution to here means the vote is different
    // we might have to decrement the old voting if they voted previously
    // and we will have to increment the new voting

    // write vote to their voting key
    ASSERT(state_set(topic_data + padding, topic_size, SBUF(account_field)) == topic_size);
    
    uint8_t previous_votes = 0;
    // decrement old vote counter for this option
    if (previous_topic_size > 0)
    {
        uint8_t votes = 0;
        // override the first two bytes to turn it into a vote count key
        previous_topic_data[0] = 'C';
        previous_topic_data[1] = t;
        previous_topic_data[2] = n;
        previous_topic_data[3] = l;

        ASSERT(state(&votes, 1, SBUF(previous_topic_data)) == 1);
        ASSERT(votes > 0);
        previous_votes = votes;
        votes--;
        // delete the state entry if votes hit zero
        ASSERT(state_set(votes == 0 ? 0 : &votes, votes == 0 ? 0 : 1, SBUF(previous_topic_data)) >= 0);
    }
    
    // increment new counter 
    uint8_t votes = 0;
    {
        // we're going to clobber the topic data to turn it into a vote count key
        // so store the first bytes 
        uint64_t saved_data = *((uint64_t*)topic_data);
        topic_data[0] = 'C';
        topic_data[1] = t;
        topic_data[2] = n;
        topic_data[3] = l;

        state(&votes, 1, topic_data, 32);
        votes++;
        ASSERT(0 < state_set(&votes, 1, topic_data, 32));

        // restore the saved bytes
        *((uint64_t*)topic_data) = saved_data;
    }
   


    if (DEBUG)
    {
        TRACEVAR(topic_data_zero);
        TRACEVAR(votes);
        TRACEVAR(member_count);
        trace(SBUF("topic"), topic, 2, 1);
    }
   
    int64_t q80 = member_count * 0.8;
    int64_t q51 = member_count * 0.51;

    if (q80 < 2)
        q80 = 2;
    
    if (q51 < 2)
        q51 = 2;

    if (is_L1_table || l == 2)
    {
       if (votes <
            (t == 'S'
                ? q80                      // L1s have 80% threshold for membership/seat voting
                : member_count))            // L1s have 100% threshold for all other voting
            DONE("Governance: Vote record. Not yet enough votes to action.");
    }
    else if (votes < q51)
    {
            // layer 2 table voting on a l1 topic
            DONE("Governance: Not yet enough votes to action L1 vote...");
    }

    

    // action vote
    if (DEBUG)
        TRACESTR("Actioning votes");

    if (l == 1 && !is_L1_table)
    {
    

        uint8_t txn_out[1024];
        uint8_t* buf_out = txn_out;
        uint32_t cls = (uint32_t)ledger_seq();
        _01_02_ENCODE_TT                   (buf_out, ttINVOKE);
        _02_02_ENCODE_FLAGS                (buf_out, tfCANONICAL);
        _02_04_ENCODE_SEQUENCE             (buf_out, 0);
        _02_26_ENCODE_FLS                  (buf_out, cls + 1);
        _02_27_ENCODE_LLS                  (buf_out, cls + 5);
        uint8_t* fee_ptr = buf_out;
        _06_08_ENCODE_DROPS_FEE            (buf_out, 0);
        _07_03_ENCODE_SIGNING_PUBKEY_NULL  (buf_out);
        _08_01_ENCODE_ACCOUNT_SRC          (buf_out, hook_accid + 12);
        _08_03_ENCODE_ACCOUNT_DST          (buf_out, genesis);
        int64_t edlen = etxn_details((uint32_t)buf_out, 512);
        buf_out += edlen;

        /** Parameters:
         * F013E017 70180154
         * 701902 
         * <00> <two byte topic code>
         * E1E0177018015670
         * 19
         * <topic len>
         * <topic data>
         * E1F1
        */

        // note wasm is LE and Txns are BE so these large constants are endian flipped
        // to undo the flipping on insertion into memory
        *((uint64_t*)buf_out) = 0x5401187017E013F0ULL; // parameters array, first parameter preamble
        buf_out += 8;
        *((uint32_t*)buf_out) = 0x021970UL;
        buf_out += 3;
        *buf_out++ = t; // topic
        *buf_out++ = n; 
        *((uint64_t*)buf_out) = 0x705601187017E0E1ULL;
        buf_out += 8;
        *buf_out++ = 0x19U;
        // topic data len
        *buf_out++ = topic_size;
        uint64_t* d = (uint64_t*)buf_out;
        uint64_t* s = (uint64_t*)(topic_data + padding);
        *d++ = *s++;
        *d++ = *s++;
        *d++ = *s++;
        *d++ = *s++;
        buf_out += topic_size;
        // topicdata
        *((uint16_t*)buf_out) = 0xF1E1U;

        int64_t txn_len = buf_out - txn_out;

        // populate fee
        int64_t fee = etxn_fee_base(txn_out, txn_len);
        _06_08_ENCODE_DROPS_FEE            (fee_ptr, fee                            );

        trace(SBUF("Governance: Emitting invoke to L1"), txn_out, txn_len, 1);


        uint8_t emit_hash[32];
        int64_t emit_result = emit(SBUF(emit_hash), txn_out, txn_len);

        trace_num(SBUF("Governance: Emit result"), emit_result);

        if (emit_result == 32)
            DONE("Governance: Successfully emitted L1 vote.");

        NOPE("Governance: L1 vote emission failed.");
    }
    
    switch(t)
    {       
        case 'H':
        {
            // hook topics

            // first get the hook ledget object
            uint8_t keylet[34];
            util_keylet(SBUF(keylet), KEYLET_HOOK, hook_accid + 12, 20, 0,0,0,0);
            slot_set(SBUF(keylet), 5);

            // now get the hooks array
            slot_subfield(5, sfHooks, 6);

            // now check the entry
            if (slot_subarray(6, n, 7) == 7)
            {
                // it exists
                // check if its identical
                uint8_t existing_hook[32];
                if (slot_subfield(7, sfHookHash, 8) == 8)
                {
                    ASSERT(slot(SBUF(existing_hook), 8) == 32);

                    // if it is then do nothing
                    if (BUFFER_EQUAL_32(existing_hook, topic_data))
                        DONE("Goverance: Target hook is already the same as actioned hook.");
                }
            }

            // generate the hook definition keylet
            if (!topic_data_zero)
            {
                util_keylet(SBUF(keylet), KEYLET_HOOK_DEFINITION, topic_data, 32, 0,0,0,0);

                // check if the ledger contains such a hook definition
                if (slot_set(SBUF(keylet), 9) != 9)
                    NOPE("Goverance: Hook Hash doesn't exist on ledger while actioning hook.");
            }

            // it does so now we can do the emit

            uint8_t* hookhash = 
                topic_data_zero 
                ? ((uint8_t*)0xFFFFFFFFU) // if the topic data is all zero then it's a delete operation
                : topic_data;             // otherwise it's an install operation


            uint8_t* h[10];
            h[n] = hookhash;

            uint8_t emit_buf[1024];
            uint32_t emit_size = 0;
            PREPARE_HOOKSET(emit_buf, sizeof(emit_buf), h, emit_size);

            trace(SBUF("EmittedTxn"), emit_buf, emit_size, 1);

            uint8_t emithash[32];
            int64_t emit_result = emit(SBUF(emithash), emit_buf, emit_size);

            if (DEBUG)
                TRACEVAR(emit_result);

            if (emit_result != 32)
                NOPE("Governance: Emit failed during hook actioning.");

            trace(SBUF("EmittedTxnHash"), emithash, 32, 1);
            DONE("Governance: Hook actioned.");
        }
    }

   rollback(SBUF("Governance: Internal logic error."), __LINE__); 
}
