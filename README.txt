Bullet Trace VFX - Documentation

1. Add Niagara system component to an actor
2. Set the Niagara system asset to NS_BulletTrace
3. In detail panel of the Niagara system set auto activate to false
4. You can set Niagara system parameters now. in BP_ExamplesParentClass there is a function called "Set Niagara Parameters", you can use it as an example or just copy paste it
5. To trigger the bullet trace from an actor you can use a "Call Niagara Bullet Effect" function. it gets as a parameter a hit from a trace, but you can also just the target point and hit point Niagara parameters directly. 
the Target point - is a trace end, it's the end point of where you aiming/tracing.
the Hit point - is the trace hit, it's the point where trace got blocked or in another words where bullet hit the target

important note:
the bullet speed should be less then time between Niagara gets called! if you want to call the Niagara more often you should make bullet speed smaller or add second Niagara system.