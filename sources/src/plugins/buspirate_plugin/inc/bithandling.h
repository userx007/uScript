#ifndef _BIT_HANDLING_H_
#define _BIT_HANDLING_H_

/* val=target variable, pos=bit number to act upon 0-n */
#define BIT_SET(val,pos)            ((val) |= (1ULL<<(pos)))
#define BIT_CLEAR(val,pos)          ((val) &= ~(1ULL<<(pos)))
#define BIT_FLIP(val,pos)           ((val) ^= (1ULL<<(pos)))
#define BIT_CHECK(val,pos)          (!!((val) & (1ULL<<(pos))))

/* val=target variable, mask=mask */
#define BITMASK_SET(val,mask)       ((val) |= (mask))
#define BITMASK_CLEAR(val,mask)     ((val) &= (~(mask)))
#define BITMASK_FLIP(val,mask)      ((val) ^= (mask))
#define BITMASK_CHECK_ALL(val,mask) (!(~(val) & (mask)))
#define BITMASK_CHECK_ANY(val,mask) ((val) & (mask))

#endif // _BIT_HANDLING_H_
