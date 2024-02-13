import {  createHookPayload } from '@transia/hooks-toolkit'
import { Client, SetHookFlags, Wallet } from '@transia/xrpl'

const wallet = Wallet.fromSecret('ss9jAKDqs7oeZkVDrmdHi2YNotSHm') // rpPrrnjgUUqy5S4LF1ukMQ4purkt9KQ4HL
const client = new Client('wss://xahau-test.net')

export async function main(): Promise<void> {
  await client.connect()
  const hook = createHookPayload({
    version: 0,
    createFile: 'govern',
    flags: SetHookFlags.hsfOverride,
    hookOnArray: ['Invoke'],
  })

  let transaction = await client.autofill({
    TransactionType: 'SetHook',
    Account: wallet.address,
    NetworkID: await client.getNetworkID(),
    Hooks: [
      { Hook: { ...hook, HookNamespace: "0000000000000000000000000000000000000000000000000000000000000000" } }
    ],
  })
  
  const tx_blob = wallet.sign(transaction).tx_blob
  
  const response = await client.submit(tx_blob)
  
  console.log(response)
  
  // await client.submitAndWait({
  //   TransactionType: 'SetHook',
  //   Account: wallet.address,
  //   Hooks: [{ Hook: hook }],
  // }, { wallet })
  
  await client.disconnect()
}

main()
