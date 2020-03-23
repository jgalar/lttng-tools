/*
 * Copyright (C) 2017 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#ifndef LTTNG_TRIGGER_H
#define LTTNG_TRIGGER_H

struct lttng_action;
struct lttng_condition;
struct lttng_trigger;

#ifdef __cplusplus
extern "C" {
#endif

enum lttng_register_trigger_status {
	LTTNG_REGISTER_TRIGGER_STATUS_OK = 0,
	LTTNG_REGISTER_TRIGGER_STATUS_INVALID = -1,
};

enum lttng_trigger_status {
	LTTNG_TRIGGER_STATUS_OK = 0,
	LTTNG_TRIGGER_STATUS_ERROR = -1,
	LTTNG_TRIGGER_STATUS_UNKNOWN = -2,
	LTTNG_TRIGGER_STATUS_INVALID = -3,
	LTTNG_TRIGGER_STATUS_UNSET = -4,
	LTTNG_TRIGGER_STATUS_UNSUPPORTED = -5,
};

/*
 * Create a trigger object associating a condition and an action.
 *
 * A trigger associates a condition and an action to take whenever the
 * condition evaluates to true. Such actions can, for example, consist
 * in the emission of a notification to clients listening through
 * notification channels.
 *
 * The caller retains the ownership of both the condition and action
 * and both must be kept alive for the lifetime of the trigger object.
 *
 * A trigger must be registered in order to become activate and can
 * be destroyed after its registration.
 *
 * Returns a trigger object on success, NULL on error.
 * Trigger objects must be destroyed using the lttng_trigger_destroy()
 * function.
 */
extern struct lttng_trigger *lttng_trigger_create(
		struct lttng_condition *condition, struct lttng_action *action);

/*
 * Get the condition of a trigger.
 *
 * The caller acquires no ownership of the returned condition.
 *
 * Returns a condition on success, NULL on error.
 */
extern struct lttng_condition *lttng_trigger_get_condition(
		struct lttng_trigger *trigger);

/*
 * Get the action of a trigger.
 *
 * The caller acquires no ownership of the returned action.
 *
 * Returns an action on success, NULL on error.
 */
extern struct lttng_action *lttng_trigger_get_action(
		struct lttng_trigger *trigger);


/*
 * Get the name of a trigger.
 *
 * The caller does not assume the ownership of the returned name.
 * The name shall only only be used for the duration of the trigger's
 * lifetime, or until a different name is set.
 *
 * Returns LTTNG_TRIGGER_STATUS_OK and a pointer to the trigger's name on
 * success, LTTNG_TRIGGER_STATUS_INVALID if an invalid parameter is passed,
 * or LTTNG_TRIGGER_STATUS_UNSET if a name was not set prior to this call.
 */
extern enum lttng_trigger_status lttng_trigger_get_name(
		struct lttng_trigger *trigger, const char **name);

/*
 * Set the trigger name.
 *
 * A name is optional.
 * A name will be assigned on trigger registration if no name is set.
 *
 * The name is copied.
 *
 * Return LTTNG_TRIGGER_STATUS_OK on success, LTTNG_TRIGGER_STATUS_INVALID
 * if invalid parameters are passed.
 */
extern enum lttng_trigger_status lttng_trigger_set_name(
		struct lttng_trigger *trigger, const char *name);

/*
 * Destroy (frees) a trigger object.
 */
extern void lttng_trigger_destroy(struct lttng_trigger *trigger);

/*
 * Register a trigger to the session daemon.
 *
 * The trigger can be destroyed after this call.
 *
 * Return 0 on success, a negative LTTng error code on error.
 */
extern int lttng_register_trigger(struct lttng_trigger *trigger);

/*
 * Unregister a trigger from the session daemon.
 *
 * The trigger can be destroyed after this call.
 *
 * Return 0 on success, a negative LTTng error code on error.
 */
extern int lttng_unregister_trigger(struct lttng_trigger *trigger);

#ifdef __cplusplus
}
#endif

#endif /* LTTNG_TRIGGER_H */
