"""A same-container helper loaded with biaseddoom.import_script."""


def actor_summary(actors):
    living = sum(1 for actor in actors if actor.alive)
    return f"{living}/{len(actors)} matching actors are alive"
