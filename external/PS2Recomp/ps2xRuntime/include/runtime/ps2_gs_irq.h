#pragma once

class PS2Runtime;

// Raises the EE INTC GS interrupt (cause 0) when the GS has latched a SIGNAL or
// FINISH event in CSR that IMR does not mask. Edge-triggered per CSR bit: a bit
// raises once and re-arms after the game clears it (CSR bits are write-1-to-clear).
// Must be called on the guest executor thread.
void ps2PollGsInterrupt(PS2Runtime *runtime);
