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
/* A collection of trigger */
struct lttng_triggers;

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

enum lttng_trigger_firing_policy_type {
	LTTNG_TRIGGER_FIRE_EVERY_N = 0,
	LTTNG_TRIGGER_FIRE_ONCE_AFTER_N = 1,
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
 * If the action is a notification action with capture descriptors,
 * the condition must be an event rule condition.
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

const struct lttng_condition *lttng_trigger_get_const_condition(
		const struct lttng_trigger *trigger);

/*
 * Get the action of a trigger.
 *
 * The caller acquires no ownership of the returned action.
 *
 * Returns an action on success, NULL on error.
 */
extern struct lttng_action *lttng_trigger_get_action(
		struct lttng_trigger *trigger);

const struct lttng_action *lttng_trigger_get_const_action(
		const struct lttng_trigger *trigger);

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
		const struct lttng_trigger *trigger, const char **name);

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
 * Set the trigger firing policy.
 *
 * This is optional. By default a trigger is set to fire each time the
 * associated condition occurs.
 *
 * Threshold is the number of time the condition must be hit before the policy is
 * enacted.
 *
 * Return LTTNG_TRIGGER_STATUS_OK on success, LTTNG_TRIGGER_STATUS_INVALID
 * if invalid parameters are passed.
 */
extern enum lttng_trigger_status lttng_trigger_set_firing_policy(
		struct lttng_trigger *trigger,
		enum lttng_trigger_firing_policy_type policy_type,
		unsigned long long threshold);
extern enum lttng_trigger_status lttng_trigger_get_firing_policy(
		const struct lttng_trigger *trigger,
		enum lttng_trigger_firing_policy_type *policy_type,
		unsigned long long *threshold);

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
extern int lttng_unregister_trigger(const struct lttng_trigger *trigger);

/*
 * List current triggers.
 *
 * On success, triggers is allocated.
 * The trigger collection must be free by the caller with lttng_destroy_triggers
 *
 * Returns 0 on success, else a negative LTTng error code.
 */
extern int lttng_list_triggers(struct lttng_triggers **triggers);

/*
 * Get a trigger from the collection at a given index.
 *
 * Note that the collection maintains the ownership of the returned trigger.
 * It must not be destroyed by the user, nor should it be held beyond the
 * lifetime of the trigger collection.
 *
 * Returns a trigger, or NULL on error.
 */
extern const struct lttng_trigger *lttng_triggers_get_at_index(
		const struct lttng_triggers *triggers, unsigned int index);

/*
 * Get the number of trigger in a tracker id list.
 *
 * Return LTTNG_TRIGGER_STATUS_OK on success,
 * LTTNG_TRIGGER_STATUS_INVALID when passed invalid parameters.
 */
extern enum lttng_trigger_status lttng_triggers_get_count(
		const struct lttng_triggers *triggers, unsigned int *count);

/*
 * Destroy a trigger collection.
 */
extern void lttng_triggers_destroy(struct lttng_triggers *ids);


#ifdef __cplusplus
}
#endif

#endif /* LTTNG_TRIGGER_H */
