import {
  SetHookFlags,
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
} from '@transia/hooks-toolkit'

// NOT EXPORTED
import {
  XrplIntegrationTestContext,
  serverUrl,
  setupClient,
} from '@transia/hooks-toolkit/dist/npm/src/libs/xrpl-helpers'

export async function main(): Promise<void> {
  const testContext = (await setupClient(
    serverUrl
  )) as XrplIntegrationTestContext

  const { alice, bob, carol, dave, elsa, frank, grace } = testContext

  const seatMember = [carol, dave, elsa, frank, grace]

  const hook = createHookPayload(
    0,
    'govern',
    '0000000000000000000000000000000000000000000000000000000000000000',
    SetHookFlags.hsfOverride,
    ['Invoke'],
    [
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
  )

  await setHooksV3({
    client: testContext.client,
    seed: alice.seed,
    hooks: [{ Hook: hook }],
  } as SetHookParams)

  // Initialize governance seat
  await Xrpld.submit(testContext.client, {
    tx: {
      TransactionType: 'Invoke',
      Account: bob.address,
      Destination: alice.address,
    },
    wallet: bob,
  })

  // Vote from carol (seat 5)
  await Xrpld.submit(testContext.client, {
    tx: {
      TransactionType: 'Invoke',
      Account: carol.address,
      Destination: alice.address,
      HookParameters: [
        new iHookParamEntry(
          new iHookParamName('L'),
          new iHookParamValue('02', true)
        ).toXrpl(),
        new iHookParamEntry(
          new iHookParamName('T'),
          new iHookParamValue(convertStringToHex('S') + 10, true)
        ).toXrpl(),
        new iHookParamEntry(
          new iHookParamName('V'),
          new iHookParamValue(
            decodeAccountID(carol.address).toString('hex').toUpperCase(),
            true
          )
        ).toXrpl(),
      ],
    },
    wallet: carol,
  })

  // remove carol from seat 1
  for (let i = 0; i < seatMember.length; i++) {
    await Xrpld.submit(testContext.client, {
      tx: {
        TransactionType: 'Invoke',
        Account: seatMember[i].address,
        Destination: alice.address,
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
    account: alice.address,
    namespace_id:
      '60E05BD1B195AF2F94112FA7197A5C88289058840CE7C6DF9693756BC6250F55',
  })

  const namespace_entries = (response.result as any).namespace_entries

  console.log("shoud be empty")
  console.log(
    namespace_entries.filter((e: any) =>
      e.HookStateKey.includes(
        decodeAccountID(carol.address).toString('hex').toUpperCase()
      ) ||
      e.HookStateData.includes(
        decodeAccountID(carol.address).toString('hex').toUpperCase()
      )
    )
  )

  await testContext.client.disconnect()
}

main()
