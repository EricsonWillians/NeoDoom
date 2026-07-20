"""Intentional import failure used only by the repository integration test."""

import biaseddoom as bd


@bd.on("tick")
def callback_that_must_be_rolled_back(event):
    bd.log("PYTEST rollback_leaked", level="error")


raise RuntimeError("intentional callback rollback test")
