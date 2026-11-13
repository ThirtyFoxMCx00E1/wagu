"""Normalize public DragonFF's clump API and legacy single-clump containers."""
def clumps(data):
    values=data.clumps if hasattr(data,'clumps') else [data]
    if not values:raise ValueError('No clumps in DFF')
    if not all(all(hasattr(c,name) for name in ('frame_list','geometry_list','atomic_list')) for c in values):
        raise ValueError('Unsupported DragonFF clump API')
    return values
