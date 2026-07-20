# Filtered Gameplay Events

This example creates one probe monster with TID 9301, damages it, kills it, and
destroys the corpse. Decorators filter its actor events by class and TID before
Python is called; a separate player-spawn handler uses a player-slot filter.
The spawn handler also demonstrates callback priority.

The operations dispatch their events synchronously, so the console clearly
shows the native gameplay call and its matching callback sequence.
