import sha512half from '@transia/xrpl/dist/npm/src/utils/hashes/sha512Half';
import {
  SetHookFlags,
  Wallet,
  convertStringToHex,
  decodeAccountID,
} from '@transia/xrpl'
import {
  createHookPayload,
  setHooksV3,
  SetHookParams,
  Xrpld,
  iHookParamEntry,
  iHookParamName,
  iHookParamValue,
  iHook,
  clearHookStateV3,
  clearAllHooksV3,
  floatToXfl,
  flipBeLe,
} from '@transia/hooks-toolkit'

// NOT EXPORTED
import {
  XrplIntegrationTestContext,
  serverUrl,
  setupClient,
  teardownClient,
} from '@transia/hooks-toolkit/dist/npm/src/libs/xrpl-helpers'

describe('govern.c', () => {
  let testContext: XrplIntegrationTestContext
  let seatMember: Wallet[] = []
  let hookWallet: Wallet
  let hookHash: string

  beforeAll(async () => {
    testContext = await setupClient(serverUrl)
    const { alice, bob, carol, dave, elsa } = testContext
    seatMember = [alice, bob, carol, dave, elsa]
    hookWallet = testContext.hook1
  })

  beforeEach(async () => {
    const acct1hook1 = createHookPayload({
      version: 0,
      createFile: 'govern',
      namespace: '0000000000000000000000000000000000000000000000000000000000000000',
      flags: SetHookFlags.hsfOverride,
      hookOnArray: ['Invoke'],
      hookParams: [
        // Initial Member Count
        new iHookParamEntry(
          new iHookParamName('IMC'),
          new iHookParamValue(seatMember.length.toString().padStart(2, '0'), true)
        ).toXrpl(),
        // Initial Seat
        ...seatMember.map((m, i) =>
          new iHookParamEntry(
            new iHookParamName(
              convertStringToHex('IS') + i.toString().padStart(2, '0'),
              true
            ),
            new iHookParamValue(
              decodeAccountID(m.address).toString('hex').toUpperCase(),
              true
            )
          ).toXrpl()
        ),
      ]
    })
    hookHash = sha512half(acct1hook1.CreateCode!)
    await setHooksV3({
      client: testContext.client,
      seed: hookWallet.seed,
      hooks: [{ Hook: acct1hook1 }],
    } as SetHookParams)

    await Xrpld.submit(testContext.client, {
      tx: {
        TransactionType: 'Invoke',
        Account: testContext.alice.address,
        Destination: hookWallet.address,
      },
      wallet: testContext.alice,
    })
  })

  afterEach(async () => {
    const clearHook = {
      Flags: SetHookFlags.hsfNSDelete,
      HookNamespace: '0000000000000000000000000000000000000000000000000000000000000000',
    } as iHook
    await clearHookStateV3({
      client: testContext.client,
      seed: testContext.hook1.seed,
      hooks: [{ Hook: clearHook }],
    } as SetHookParams)

    await clearAllHooksV3({
      client: testContext.client,
      seed: testContext.hook1.seed,
    } as SetHookParams)
  })

  afterAll(() => {
    teardownClient(testContext)
  })

  const createAliceVote = async (data: { layer: '01' | '02', topic: string, vote: string }) => {
    const HookParameters =  [
      new iHookParamEntry(
        new iHookParamName('L'),
        new iHookParamValue(data.layer, true)
      ).toXrpl(),
      new iHookParamEntry(
        new iHookParamName('T'),
        new iHookParamValue(data.topic, true)
      ).toXrpl(),
      new iHookParamEntry(
        new iHookParamName('V'),
        new iHookParamValue(
          data.vote,
          true
        )
      ).toXrpl(),
    ]
    await Xrpld.submit(testContext.client, {
      tx: {
        TransactionType: 'Invoke',
        Account: seatMember[0].address,
        Destination: hookWallet.address,
        HookParameters,
      },
      wallet: seatMember[0],
    })
  }

  test('Remove Alice', async () => {
    // Vote from alice (seat 0)
    // seat
    await createAliceVote({
      layer: '01',
      topic: convertStringToHex('S') + '01',
      vote: decodeAccountID(seatMember[0].address).toString('hex').toUpperCase()
    })
    await createAliceVote({
      layer: '02',
      topic: convertStringToHex('S') + '01',
      vote: decodeAccountID(seatMember[0].address).toString('hex').toUpperCase()
    })
    // hookhash
    await createAliceVote({
      layer: '01',
      topic: convertStringToHex('H') + '01',
      vote: hookHash.toUpperCase()
    })
    await createAliceVote({
      layer: '02',
      topic: convertStringToHex('H') + '01',
      vote: hookHash.toUpperCase()
    })
    // reward rate (Only L1)
    await createAliceVote({
      layer: '01',
      topic: convertStringToHex('RR'),
      vote: flipBeLe(floatToXfl(0.003) as bigint)
    })
    // reward delay (Only L1)
    await createAliceVote({
      layer: '01',
      topic: convertStringToHex('RD'),
      vote: flipBeLe(floatToXfl(30000) as bigint)
    })

    // remove alice from seat 1
    for (let i = 1; i < seatMember.length; i++) {
      await Xrpld.submit(testContext.client, {
        tx: {
          TransactionType: 'Invoke',
          Account: seatMember[i].address,
          Destination: hookWallet.address,
          HookParameters: [
            new iHookParamEntry(
              new iHookParamName('L'),
              new iHookParamValue('02', true)
            ).toXrpl(),
            new iHookParamEntry(
              new iHookParamName('T'),
              new iHookParamValue(convertStringToHex('S') + '00', true)
            ).toXrpl(),
            new iHookParamEntry(
              new iHookParamName('V'),
              new iHookParamValue(
                '0000000000000000000000000000000000000000',
                true
              )
            ).toXrpl(),
          ],
        },
        wallet: seatMember[i],
      })
    }
    const response = await testContext.client.request({
      command: 'account_namespace',
      account: hookWallet.address,
      namespace_id:
        '60E05BD1B195AF2F94112FA7197A5C88289058840CE7C6DF9693756BC6250F55',
    })

    const namespace_entries = (response.result as any).namespace_entries

    const aliceAccountId = decodeAccountID(seatMember[0].address).toString('hex').toUpperCase()

    // shoud be empty
    const aliceVotes = namespace_entries.filter((e: any) =>
      e.HookStateKey.includes(aliceAccountId) || e.HookStateData.includes(aliceAccountId)
    )
    expect(aliceVotes).toHaveLength(0)
  })
})
