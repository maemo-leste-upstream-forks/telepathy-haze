
"""
Test connecting to a server.
"""

from hazetest import exec_test

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[1, 1])
    q.expect('stream-authenticated')

    # FIXME: unlike Gabble, Haze does not signal a presence update to
    # available during connect
    #q.expect('dbus-signal', signal='PresenceUpdate',
    #    args=[{1L: (0L, {u'available': {}})}])

    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])
    return True

if __name__ == '__main__':
    exec_test(test)

