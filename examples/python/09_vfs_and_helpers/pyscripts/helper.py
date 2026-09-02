"""A same-container helper loaded with biaseddoom.import_script.

Two trivial functions on purpose: main.py calls both to show that an imported
script is a full module object, not a single callable.
"""


def greeting():
    return "helper.py (imported from this PK3) says hello"


def actor_summary(actors):
    living = sum(1 for actor in actors if actor.alive)
    return f"{living}/{len(actors)} matching actors are alive"
