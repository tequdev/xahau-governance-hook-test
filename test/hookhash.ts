import sha512half from '@transia/xrpl/dist/npm/src/utils/hashes/sha512Half'
import {
  createHookPayload,
} from '@transia/hooks-toolkit'


export async function main(): Promise<void> {
  const hook = createHookPayload(0, 'govern')

  if (hook.CreateCode) {
    console.log("HookHash", sha512half(hook.CreateCode))
  } else {
    throw new Error('Cannot retrieve CreateCode.')
  }
}

main()
