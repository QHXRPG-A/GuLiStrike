"""Current card content ownership. Marketplace assets are never renamed."""
SYSTEM = '/Game/GuLiStrike/CardSystem'
EFFECT_ROOT = '/Game/GuLiStrike/Cards/Commander/WM01'
EFFECTS = {'FireRate': 'FireRate', 'MissileDamage': 'MissileDamage', 'HighSpeed': 'MoveSpeed'}


def destination(package):
    if package.startswith('/Game/GuLiStrike/Cards/RevealDemo/'):
        return package.replace('/Game/GuLiStrike/Cards/RevealDemo/', SYSTEM + '/RevealDemo/', 1)
    old = '/Game/GuLiStrike/Cards/WarMachineTarot/'
    if package.startswith(old):
        suffix = package[len(old):]
        for token, effect in EFFECTS.items():
            if token in suffix.rsplit('/', 1)[-1]:
                return EFFECT_ROOT + '/' + effect + '/' + suffix
        return SYSTEM + '/WarMachineTarot/' + suffix
    return package
