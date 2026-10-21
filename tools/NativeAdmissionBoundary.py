"""Strictly separate one accounted admission notification from drawing code."""

NATIVE_ADMISSION = '    resourcepacks::state().submitted(resourcepacks::Domain::Projection);\n'


def detach_native_admission(body):
    """Require the notification as the unique first statement, then detach it.

    This verifies its production presence and ordering independently. The
    caller must still compare the entire remaining drawing body verbatim.
    It neither removes a drawing command nor supplies a GPU-completion fact.
    """
    assert body.count(NATIVE_ADMISSION) == 1, 'one exact native admission notification required'
    opening = body.index('{')
    assert body[opening + 1:].startswith('\n' + NATIVE_ADMISSION), 'native admission must be the first statement'
    return body.replace(NATIVE_ADMISSION, '', 1)
