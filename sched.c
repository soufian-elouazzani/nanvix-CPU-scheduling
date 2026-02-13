/*
 * Copyright(C) 2011-2016 Pedro H. Penna   <pedrohenriquepenna@gmail.com>
 *              2015-2016 Davidson Francis <davidsondfgl@hotmail.com>
 *
 * This file is part of Nanvix.
 *
 * Nanvix is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Nanvix is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Nanvix. If not, see <http://www.gnu.org/licenses/>.
 */

#include <nanvix/clock.h>
#include <nanvix/const.h>
#include <nanvix/hal.h>
#include <nanvix/pm.h>
#include <signal.h>

/**
 * @brief Schedules a process to execution.
 *
 * @param proc Process to be scheduled.
 */
PUBLIC void sched(struct process *proc)
{
	proc->state = PROC_READY;

}

/**
 * @brief Stops the current running process.
 */
PUBLIC void stop(void)
{
	curr_proc->state = PROC_STOPPED;
	sndsig(curr_proc->father, SIGCHLD);
	yield();
}

/**
 * @brief Resumes a process.
 *
 * @param proc Process to be resumed.
 *
 * @note The process must stopped to be resumed.
 */
PUBLIC void resume(struct process *proc)
{
	/* Resume only if process has stopped. */
	if (proc->state == PROC_STOPPED)
		sched(proc);
}

/**
 * @brief Ordonnanceur à priorités fixes.
 * 
 * @details Sélectionne le processus prêt avec la priorité la plus élevée.
 *          Dans Nanvix, une valeur de priorité plus petite = priorité plus haute.
 */
PUBLIC void yield(void)
{
	struct process *p;
	struct process *next;

	/* Remet le processus courant dans la file prête */
	if (curr_proc->state == PROC_RUNNING)
		sched(curr_proc);

	/* Sauvegarde pour débogage */
	last_proc = curr_proc;

	/* Vérification des alarmes */
	for (p = FIRST_PROC; p <= LAST_PROC; p++)
	{
		if (!IS_VALID(p))
			continue;

		if ((p->alarm) && (p->alarm < ticks))
			p->alarm = 0, sndsig(p, SIGALRM);
	}

	/*
	 * ORDONNANCEMENT PAR PRIORITÉS FIXES
	 * Principe : élire le processus prêt avec la plus petite valeur de priorité
	 */
	next = IDLE;  /* Processus par défaut */

	for (p = FIRST_PROC; p <= LAST_PROC; p++)
	{
		if (!IS_VALID(p))
			continue;

		if (p->state != PROC_READY)
			continue;

		/* Sélection du processus avec la priorité la plus élevée */
		if ((next == IDLE) || (p->priority < next->priority))
			next = p;
	}

	/* Vérification de sécurité */
	if (!IS_VALID(next))
		next = IDLE;

	/* Prépare le processus élu */
	next->priority = PRIO_USER;  /* Réinitialise la priorité */
	next->state = PROC_RUNNING;
	next->counter = PROC_QUANTUM;  /* Nouveau quantum */

	/* Changement de contexte si nécessaire */
	if (curr_proc != next)
		switch_to(next);
}
